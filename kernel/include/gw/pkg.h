#ifndef GW_PKG_H
#define GW_PKG_H

/*
 * gwpkg — repository-based package manager (not ESP-local).
 *
 * Package sources live in remote repositories (HTTP), listed in:
 *   C:/etc/gwpkg/sources
 * Downloaded artifacts cache:
 *   C:/var/cache/gwpkg/
 * Installed files:
 *   C:/Program Files/<NAME>/
 *
 * ESP (D:) is boot-only — never a package store.
 */

void pkg_init(void);
void pkg_list(void);                      /* configured repos + cached pkgs */
int  pkg_repo_add(const char *url);
void pkg_repo_list(void);
int  pkg_info(const char *name);
int  pkg_install(const char *name);
int  pkg_fetch(const char *name);         /* download into cache only */

#endif
