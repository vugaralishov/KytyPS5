#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_

#include "common/common.h"
#include "graphics/host_gpu/regionDefinitions.h"

#include <memory>

namespace Libs::Graphics {

enum class PageFaultAccess { Read, Write, Execute, Unknown };

class PageManager final {
public:
	PageManager();
	// The owner must stop all PageManager callers before destruction.
	~PageManager();

	KYTY_CLASS_NO_COPY(PageManager);

	[[nodiscard]] uint64_t GetPageSize() const;
	// Whether watchers currently deny this access to the page holding vaddr. A write is denied
	// by any watcher, other accesses only by access watchers; Unknown accepts any watcher.
	[[nodiscard]] bool IsAccessWatched(uint64_t vaddr, PageFaultAccess access) const;
	// Sets the bits of the pages in [first, last) of the tracking region at base_addr that the
	// host never protects (guest thread stacks on Windows). Watchers on those pages never fault,
	// so trackers must keep them CPU-dirty instead of relying on write faults.
	void MarkUnprotectablePages(uint64_t base_addr, size_t first, size_t last,
	                            RegionBits& pages) const;

	template <bool track>
	void UpdatePageWatchers(uint64_t vaddr, uint64_t size);
	template <bool track, bool is_read = false>
	void UpdatePageWatchersForRegion(uint64_t base_addr, RegionBits& mask);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_
