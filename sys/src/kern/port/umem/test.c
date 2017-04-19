/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2016 Giacomo Tesio <giacomo@tesio.it>
 *
 * Jehanne is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/umem/internals.h"

int enable_umem_tests_panics = 0;

void
umem_test_virtual_pages(void)
{
	PagePointer page = 0;
	PagePointer tmp, neverAgain;
	uintptr_t pagepa, tmppa;

	if(!page_new(&page, 1))
		panic("page_new: failed 1");
	if(page == 0)
		panic("page_new: failed 2");
	tmp = page;
	if(!page_dispose(&page))
		panic("page_dispose: failed 1");
	if(page != 0)
		panic("page_dispose: failed 2");
	if(!page_new(&page, 1))
		panic("page_new: failed 1");
	if(page != tmp)
		panic("page_new: ignoring free page");
	tmp = 0;
	if(!page_new(&tmp, 0))
		panic("page_new: failed 3");
	if(tmp == page)
		panic("page_new: active page reused");
	neverAgain = tmp;
	if(!page_dispose(&tmp))
		panic("page_dispose: failed 3");
	if(tmp != 0)
		panic("page_dispose: failed 4");
	if(!page_assign(&tmp, page))
		panic("page_assign: failed 1");
	if(tmp != page)
		panic("page_assign: failed 2");
	pagepa = page_pa(page);
	tmppa = page_pa(tmp);
	if(pagepa != tmppa)
		panic("page_assign: failed 2");
	if(!page_dispose(&tmp))
		panic("page_dispose: failed 5");
	if(tmp != 0)
		panic("page_dispose: failed 6");
	/* now page ref count should be 1 */
	pagepa = page_pa(page);
	if(pagepa != tmppa)
		panic("page_pa: failed 1");

	if(enable_umem_tests_panics){
		page_pa(neverAgain);
		page_dispose(&neverAgain);
	}
}

void
umem_test_raw_memory(void)
{
	char* name;
	uintptr_t pa;
	unsigned int attr;
	usize size;
	PagePointer page = 0;

	rawmem_register("vesascreen", 0xfc000000, SgPhysical, 0x1000000/PGSZ);

	if(!rawmem_lookup(0xfc000000, &name, &pa, &attr, &size, &page))
		panic("rawmem_lookup: failed 1");
	if(jehanne_strcmp(name, "vesascreen") != 0)
		panic("rawmem_lookup: failed 2");
	if(pa != 0xfc000000)
		panic("rawmem_lookup: failed 3");
	if(attr != SgPhysical)
		panic("rawmem_lookup: failed 4");
	if(size != 0x1000000/PGSZ)
		panic("rawmem_lookup: failed 5");
	if(page == 0)
		panic("rawmem_lookup: failed 6");
	if(page_pa(page) != 0xfc000000)
		panic("rawmem_lookup: failed 7");
	if(page_pa(page+1) != 0xfc001000)
		panic("rawmem_lookup: failed 8");
}

void
print_type_sizes(void)
{
	jehanne_print("sizeof UserPage %d bytes\n", sizeof(UserPage));
	jehanne_print("sizeof PageTableEntry %d bytes\n", sizeof(PageTableEntry));
	jehanne_print("sizeof PageTable %d bytes\n", sizeof(PageTable));
	jehanne_print("sizeof ElfSegment %d bytes\n", sizeof(ElfSegment));
	jehanne_print("sizeof ElfImage %d bytes\n", sizeof(ElfImage));
	jehanne_print("sizeof ProcSegment %d bytes\n", sizeof(ProcSegment));
	jehanne_print("sizeof RawMemory %d bytes\n", sizeof(RawMemory));
print("---\n");
	jehanne_print("sizeof Page %d bytes\n", sizeof(Page));
	jehanne_print("sizeof Pte %d bytes\n", sizeof(Pte));
	jehanne_print("sizeof Pages %d bytes\n", sizeof(Pages));
	jehanne_print("sizeof Section %d bytes\n", sizeof(Section));
	jehanne_print("sizeof Image %d bytes\n", sizeof(Image));
	jehanne_print("sizeof Segment %d bytes\n", sizeof(Segment));
	jehanne_print("sizeof Physseg %d bytes\n", sizeof(Physseg));
print("---\n");
	jehanne_print("sizeof Proc %d bytes\n", sizeof(Proc));

}

void
umem_test(void)
{
	print_type_sizes();

	umem_test_virtual_pages();
	umem_test_raw_memory();
}
