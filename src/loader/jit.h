#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_JIT_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_JIT_H_

#include "common/abi.h"
#include "common/assert.h"

#include <cinttypes>
#include <cstdint>

namespace Loader::Jit {

#pragma pack(1)

struct JmpRax {
	template <class Handler>
	void SetFunc(Handler func) {
		*reinterpret_cast<Handler*>(&code[2]) = func;
	}

	// mov rax, 0x1122334455667788
	// jmp rax
	uint8_t code[16] = {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xE0};
};

// The handler region is allocated next to the program image, so a rel32 call always reaches it.
// Anything else is a loader bug: fail loudly instead of emitting a call to a wrong address.
inline uint32_t Rel32(uint64_t target, uint64_t next_rip) {
	const auto offset64 = static_cast<int64_t>(target - next_rip);
	if (offset64 < INT32_MIN || offset64 > INT32_MAX) {
		EXIT("Jit: call target %016" PRIx64 " is out of rel32 range from %016" PRIx64 "\n", target,
		     next_rip);
	}
	return static_cast<uint32_t>(static_cast<int32_t>(offset64));
}

struct Call9 {
	template <class Handler>
	void SetFunc(Handler func) {
		*reinterpret_cast<uint32_t*>(&code[2]) =
		    Rel32(reinterpret_cast<uint64_t>(reinterpret_cast<void*>(func)),
		          reinterpret_cast<uint64_t>(&code[6]));
	}

	void SetOutputReg(uint8_t reg) { code[8] = 0xc0u | (reg & 7u); }

	static uint64_t GetSize() { return 9; }

	// rex.w; call func
	// mov rax,rax
	// REX.W is required because AMD processors honor a retained 0x66 operand-size
	// prefix on a near call and would otherwise execute callw.
	uint8_t code[9] = {0x48, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xC0};
};

struct TlsRegStub {
	template <class Handler>
	void SetFunc(Handler func) {
		*reinterpret_cast<uint32_t*>(&code[9]) =
		    Rel32(reinterpret_cast<uint64_t>(reinterpret_cast<void*>(func)),
		          reinterpret_cast<uint64_t>(&code[13]));
	}

	void SetOutputReg(uint8_t reg) { code[15] = 0xc0u | (reg & 7u); }

	static uint64_t GetOffset(uint8_t reg) {
		return 0x100 + static_cast<uint64_t>(reg) * GetSize();
	}
	static uint64_t GetSize() { return 32; }

	// sub rsp,0x80
	// push rax
	// call safe_call
	// mov <reg>,rax
	// pop rax
	// add rsp,0x80
	// ret
	uint8_t code[32] = {0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x50, 0xE8, 0x00, 0x00,
	                    0x00, 0x00, 0x48, 0x89, 0xC0, 0x58, 0x48, 0x81, 0xC4, 0x80, 0x00,
	                    0x00, 0x00, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
};

// Replacement for guest `mov <reg>, fs:[0]`: calls the MS-ABI TlsMainGetAddr while preserving
// every register the guest instruction would not have touched.
//
// Stack layout (growing down from the guest rsp R at the patched instruction):
//   R-0x08          return address pushed by the guest-side Call9 (or by TlsRegStub)
//   R-0x88..R-0x09  skipped 0x80 bytes so the rest of the SysV red zone stays intact (the 9-byte
//                   call patch itself unavoidably overwrites its top 8 bytes)
//   below that      rflags, rcx, rdx, r8-r11, rbx, rdi, rsi (rbx keeps this rsp)
//   and rsp,-16     realign for the host call, then reserve 0x80 bytes:
//     [rsp+0x00..0x1f]  MS-ABI shadow space for the callee
//     [rsp+0x20..0x7f]  xmm0-xmm5, which are volatile in the MS ABI but not clobbered by the
//                       original guest instruction; the slow path runs memset/memcpy/maps
//   rsp stays 16-byte aligned at the call, so the callee sees the usual rsp % 16 == 8.
// rax returns the TCB address; TlsRegStub saves rax itself when another register is the output.
// cld is required by the MS ABI at call boundaries; popfq restores the guest DF afterwards.
struct SafeCall {
	using func_t = KYTY_MS_ABI uint8_t* (*)();

	void SetFunc(func_t func) { *reinterpret_cast<func_t*>(&code[0x4a]) = func; }

	static uint64_t GetSize() { return 0x1000; }

	uint8_t code[0xa0] = {
	    /*00*/ 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, // sub    rsp,0x80
	    /*07*/ 0x9c,                                     // pushfq
	    /*08*/ 0x51,                                     // push   rcx
	    /*09*/ 0x52,                                     // push   rdx
	    /*0a*/ 0x41, 0x50,                               // push   r8
	    /*0c*/ 0x41, 0x51,                               // push   r9
	    /*0e*/ 0x41, 0x52,                               // push   r10
	    /*10*/ 0x41, 0x53,                               // push   r11
	    /*12*/ 0x53,                                     // push   rbx
	    /*13*/ 0x57,                                     // push   rdi
	    /*14*/ 0x56,                                     // push   rsi
	    /*15*/ 0x48, 0x89, 0xe3,                         // mov    rbx,rsp
	    /*18*/ 0x48, 0x83, 0xe4, 0xf0,                   // and    rsp,0xfffffffffffffff0
	    /*1c*/ 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, // sub    rsp,0x80
	    /*23*/ 0xfc,                                     // cld
	    /*24*/ 0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x20,       // movdqu [rsp+0x20],xmm0
	    /*2a*/ 0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x30,       // movdqu [rsp+0x30],xmm1
	    /*30*/ 0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x40,       // movdqu [rsp+0x40],xmm2
	    /*36*/ 0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x50,       // movdqu [rsp+0x50],xmm3
	    /*3c*/ 0xf3, 0x0f, 0x7f, 0x64, 0x24, 0x60,       // movdqu [rsp+0x60],xmm4
	    /*42*/ 0xf3, 0x0f, 0x7f, 0x6c, 0x24, 0x70,       // movdqu [rsp+0x70],xmm5
	    /*48*/ 0x48, 0xb9, 0x88, 0x77, 0x66, 0x55, 0x44,
	    0x33,        0x22, 0x11,                   // movabs rcx,0x1122334455667788
	    /*52*/ 0xff, 0xd1,                         // call   rcx
	    /*54*/ 0x48, 0x89, 0xc1,                   // mov    rcx,rax
	    /*57*/ 0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x20, // movdqu xmm0,[rsp+0x20]
	    /*5d*/ 0xf3, 0x0f, 0x6f, 0x4c, 0x24, 0x30, // movdqu xmm1,[rsp+0x30]
	    /*63*/ 0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x40, // movdqu xmm2,[rsp+0x40]
	    /*69*/ 0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x50, // movdqu xmm3,[rsp+0x50]
	    /*6f*/ 0xf3, 0x0f, 0x6f, 0x64, 0x24, 0x60, // movdqu xmm4,[rsp+0x60]
	    /*75*/ 0xf3, 0x0f, 0x6f, 0x6c, 0x24, 0x70, // movdqu xmm5,[rsp+0x70]
	    /*7b*/ 0x48, 0x89, 0xdc,                   // mov    rsp,rbx
	    /*7e*/ 0x48, 0x89, 0xc8,                   // mov    rax,rcx
	    /*81*/ 0x5e,                               // pop    rsi
	    /*82*/ 0x5f,                               // pop    rdi
	    /*83*/ 0x5b,                               // pop    rbx
	    /*84*/ 0x41, 0x5b,                         // pop    r11
	    /*86*/ 0x41, 0x5a,                         // pop    r10
	    /*88*/ 0x41, 0x59,                         // pop    r9
	    /*8a*/ 0x41, 0x58,                         // pop    r8
	    /*8c*/ 0x5a,                               // pop    rdx
	    /*8d*/ 0x59,                               // pop    rcx
	    /*8e*/ 0x9d,                               // popfq
	    /*8f*/ 0x48, 0x81, 0xc4, 0x80, 0x00, 0x00,
	    0x00,        // add    rsp,0x80
	    /*96*/ 0xc3, // ret
	};
};

// TlsRegStub copies start at TlsRegStub::GetOffset(0) inside the same handler page.
static_assert(sizeof(SafeCall) <= 0x100);

#pragma pack()

} // namespace Loader::Jit

#endif /* EMULATOR_INCLUDE_EMULATOR_LOADER_JIT_H_ */
