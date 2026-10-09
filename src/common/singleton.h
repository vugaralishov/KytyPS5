#ifndef KYTY_COMMON_SINGLETON_H_
#define KYTY_COMMON_SINGLETON_H_

#include <cstdlib>
#include <new>

namespace Common {

template <class T>
class Singleton {
public:
	static T* Instance() {
		// A function-local static is initialized exactly once even when several threads race
		// the first call. The object is intentionally never destroyed.
		static T* const instance = [] {
			// NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
			auto* storage = static_cast<T*>(std::malloc(sizeof(T)));
			return new (storage) T;
		}();

		return instance;
	}

	KYTY_CLASS_NO_COPY(Singleton);

protected:
	Singleton();
	~Singleton();
};

// template<class T> T* Singleton<T>::instance = 0;

} // namespace Common

#endif /* KYTY_COMMON_SINGLETON_H_ */
