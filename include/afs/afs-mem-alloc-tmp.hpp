#pragma once

#if defined(__GNUC__) || !defined(__apple_build_version__)
	// foonathan::memory::temporary_allocator appears to be completely
	// broken on these platforms `\('~')/`
#	define NO_TMP_ALLOC 1
#else
#	define NO_TMP_ALLOC 0
#endif

#if NO_TMP_ALLOC
#	include <foonathan/memory/new_allocator.hpp>
#else
#	include <foonathan/memory/temporary_allocator.hpp>
#endif

namespace afs::mem::alloc {

#if NO_TMP_ALLOC
	using tmp = foonathan::memory::new_allocator;
#else
	using tmp = foonathan::memory::temporary_allocator;
#endif

} // afs::mem::alloc
