/* Load a shared object at run time.
   Copyright (C) 1995-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <dlfcn.h>
#include <libintl.h>
#include <stddef.h>
#include <unistd.h>
#include <ldsodefs.h>

#if !defined SHARED && IS_IN (libdl)

void *
dliopen (const char *file, int mode)
{
  return __dliopen (file, mode, RETURN_ADDRESS (0));
}
static_link_warning (dliopen)

#else



/* Non-shared code has no support for multiple namespaces.  */
# ifdef SHARED
#  define NS __LM_ID_CALLER
# else
#  define NS LM_ID_BASE
# endif

/*
 * ZZZ: loading a DSO in a new namespace.
 *
 * In terms of functionality, `dliopen(file, mode)` should be equivalent to what `dlmopen(LM_ID_NEWLM, file, mode)`
 * is supposed to do. Its implementation, nonetheless, is very different. The main struct that glibc uses for tracking
 * loaded DSO's is `link_map`. These objects are then linked to each other to form linked-lists which are later used
 * for symbol lookups. The idea behind `dlmopen` is that it isolates DSO's by putting them into disjoint linked-lists.
 * AFAICT, there is no fundamental issue in this idea, but in practice, there are many limitations such as:
 *   - a `dlmopen`ed DSO cannot create threads,
 *   - if a `dlmopen`ed DSO tries to `dlopen(..., RTLD_GLOBAL)`, it causes segfault.
 * As a result of these limitations, I decided to try a different approach.
 *
 * In `dliopen`, DSO's in different namespaces coexist on the same linked-lists. At the symbol lookup time, though,
 * we only allow for symbols from DSO's belonging to the same namespace to match with each other. Needless to say,
 * when trying to load a new DSO, the namespace should be inherited from the caller unless we are `dliopen`ing which
 * will generate a new number.
 *
 * Another change needed for this to work is to allow for multiple DSO's with the same filename. For example, if the
 * main program tries to load libmkl.so and later on libpython.so also tries to load a potentially different version
 * of libmkl.so, we need to allow both to be loaded. By default, this is not allowed in glibc, but we can relax this
 * limitation by allowing one DSO *per namespace*.
 *
 * So far things seem reasonable, but this won't quite work as there are certain data symbols that have to be shared.
 * One example is symbols defined in glibc DSO's. They use ld.so's internal variables too and allowing for multiple
 * glibc DSO's touching those unique variables inside ld.so doesn't seem like a robust idea. One example is `environ`
 * defined in libc.so. It by default points to a an array of strings defined in ld.so. When someone calls `setenv`,
 * a new array is created with one more string and the previous array is destroyed. Now, if another namespace also
 * has a reference to `environ`, it will still point to the old array which might later be used by `malloc` as ld.so
 * calls `free()` on the old array. In order to avoid issues like this, I mark some DSO's as "universal" which means
 * that any symbol in these universal DSO's have to be thought of as shared between all namespaces.
 *
 * Other than glibc DSO's, there is one more DSO that we need to mark as universal. That one is libstdc++.so because
 * of `STB_GNU_UNIQUE` symbols. So, what we do is that we mark all of glibc DSO's and libstdc++.so as universal and
 * a symbol is universal if and only if it appears in at least one universal DSO. Then, at symbol lookup time, these
 * universal symbols can be found in link maps regardless of their namespaces.
 *
 * One last exception for the lookup rule is the DSO loaded using `dliopen` itself. When a shared object is loaded
 * using `dliopen`, the caller will need to be able to look up symbols in the loaded object. For that reason, we add
 * one new variable, called `iparent`, that points to the caller's `link_map`.
 *
 */
void *
__dliopen (const char *file, int mode, void * caller)
{
  Lmid_t new_id = GLRO(dl_zzz_get_new_inner_nsid)();

  struct link_map *result = (struct link_map *)__dlopen_with_args (file, mode, new_id, caller);

  result -> iparent = _dl_find_dso_for_object(caller);

  return result;
}

# ifdef SHARED

void *
dliopen (const char *file, int mode) {
  return __dliopen (file, mode, RETURN_ADDRESS (0));
}

# endif

#endif
