/*
 * This file is part of Jehanne.
 *
 * Copyright (C) 2017 Giacomo Tesio <giacomo@tesio.it>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * Jehanne is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Jehanne.  If not, see <http://www.gnu.org/licenses/>.
 */
extern void __libposix_files_check_conf(void);
extern void __libposix_errors_check_conf(void);
extern void __libposix_processes_check_conf(void);
extern int __libposix_initialized(void);

extern int __libposix_get_errno(PosixError e);

extern void __libposix_setup_exec_environment(char * const *env);

extern int __libposix_translate_errstr(uintptr_t caller);

extern int __libposix_note_handler(void *ureg, char *note);
