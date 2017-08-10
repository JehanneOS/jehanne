/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
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
#include "ureg.h"
#include "multiboot.h"

#define AT_KERNEL(p) ((void*)(p+KZERO))

extern int e820map(int type, uintptr_t base, uintptr_t top);

int
multiboot(int just_log)
{
	uint32_t magic;
	char *p, *modname;
	int i, n;
	multiboot_info_t *mbi;
	multiboot_module_t *mod;
	multiboot_memory_map_t *mmap;
	uint64_t addr, len;

	magic = (uint32_t)sys->boot_regs->ax;
	mbi = (multiboot_info_t*)sys->boot_regs->bx;

	if(just_log)
		jehanne_print("magic %#ux infos at %#p\n", magic, mbi);
	if(magic != 0x2badb002)
		return -1;

	if(just_log)
		jehanne_print("flags %#ux\n", mbi->flags);
	if(mbi->flags & MULTIBOOT_INFO_CMDLINE){
		p = AT_KERNEL(mbi->cmdline);
		if(just_log)
			jehanne_print("Multiboot Command Line:\n\t%s\n", p);
		else
			optionsinit(p);
	}
	if(mbi->flags & MULTIBOOT_INFO_MODS){
		if(just_log)
			jehanne_print("Multiboot Modules:\n");
		for(i = 0; i < mbi->mods_count; i++){
			mod = AT_KERNEL(mbi->mods_addr);
			if(mod->cmdline != 0)
				p = AT_KERNEL(mod->cmdline);
			else
				p = "";
			if(just_log)
				jehanne_print("\tModule <%s> %#ux %#ux\n",
					mod->mod_start, mod->mod_end, p);
			else {
				asmmodinit(mod->mod_start, mod->mod_end, p);
				modname = jehanne_strrchr(p, '/');
				if(modname == nil)
					modname = p;
				if(*modname == '/')
					++modname;
				addbootfile(modname, AT_KERNEL(mod->mod_start), mod->mod_end - mod->mod_start);
			}
		}
	}
	if(mbi->flags & MULTIBOOT_INFO_MEM_MAP){
		if(just_log)
			jehanne_print("Multiboot Memory Map:\n");
		mmap = AT_KERNEL(mbi->mmap_addr);
		n = 0;
		while(n < mbi->mmap_length){
			addr = mmap->addr;
			len = mmap->len;
			switch(mmap->type){
			default:
				if(just_log)
					jehanne_print("\ttype %ud ", mmap->type);
				break;
			case MULTIBOOT_MEMORY_AVAILABLE:
				if(just_log)
					jehanne_print("\tMemory");
				break;
			case MULTIBOOT_MEMORY_RESERVED:
				if(just_log)
					jehanne_print("\tReserved");
				break;
			case 3:
				if(just_log)
					jehanne_print("\tACPI Reclaim Memory");
				break;
			case 4:
				if(just_log)
					jehanne_print("\tACPI NVS Memory");
				break;
			}
			if(just_log)
				jehanne_print("\n\t  %#16.16llux %#16.16llux (%llud)\n",
					addr, addr+len, len);
			else
				e820map(mmap->type, addr, addr+len);

			n += mmap->size+sizeof(mmap->size);
			mmap = AT_KERNEL(mbi->mmap_addr+n);
		}
	}
	if(just_log && (mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)){
		p = AT_KERNEL(mbi->boot_loader_name);
		jehanne_print("Multiboot: Boot Loader Name <%s>\n", p);
	}

	return 0;
}
