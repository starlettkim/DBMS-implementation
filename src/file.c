#include "file.h"
#include <stdio.h>
#include <stdlib.h>


// GLOBALS & CONSTANTS.

static FILE * g_db_file;

Page * g_header_page;
Page * g_root_page;


// FUNCTION DEFINITIONS.

/* Open existing data file using ‘pathname’ or create one if not existed.
 * If success, return 0. Otherwise, return -1.
 */
int open_db(const char * pathname) {
    // Clean previous data.
    if (g_db_file) fclose(g_db_file);
    if (g_header_page) free_page(g_header_page);
    if (g_root_page) free_page(g_root_page);

    // Read existing DB file.
    if ((g_db_file = fopen(pathname, "r+"))) {
        g_header_page = read_page(0);
        if (!g_header_page) return -1;
        g_root_page   = read_page(HEADER(g_header_page)->root_page_offset);
        if (!g_root_page) return -1;

    // If not, make new.
    } else {
        g_db_file = fopen(pathname, "w+");
        if (!g_db_file) return -1;

        g_header_page = get_new_page(HEADER_PAGE);
        g_root_page   = get_new_page(INTERNAL_PAGE);
        if (write_page(g_header_page) ||
            write_page(g_root_page)) return -1;
    }

    return 0;
}

/* Close opened database file.
 * If success, return 0. Otherwise, return -1.
 */
int close_db() {
    fflush(g_db_file);
    return -(fclose(g_db_file) != 0);
}

/* Free memory of page.
 * free(page); free(page->ptr_page);
 */
void free_page(Page * page) {
    if (!page) return;
    if (page->ptr_page) {
        free(page->ptr_page);
    }
    free(page);
}

/* Read single page at given offset (of g_db_file).
 * If success, return pointer to the page. Otherwise, return NULL.
 */
Page * read_page(off_t offset) {
    // Read preparation
    if (!g_db_file) return NULL;
    if ((offset & 7) || fseeko(g_db_file, offset, SEEK_SET)) return NULL;
    
    // Memory allocation
    Page * page = calloc(1, sizeof(Page));
    if (!page) return NULL;
    page->ptr_page = calloc(1, PAGE_SIZE);
    if (!page->ptr_page) return NULL;

    page->offset = offset;

    // Read
    if (fread(page->ptr_page, PAGE_SIZE, 1, g_db_file) != PAGE_SIZE) {
        free_page(page);
        return NULL;
    }

    return page;
}

/* Write single page.
 * If success, return 0. Otherwise, return -1.
 */
int write_page(const Page * const page) {
    // Write preparation
    if (!g_db_file) return -1;
    if ((page->offset & 7) || fseeko(g_db_file, page->offset, SEEK_SET)) return -1;

    // Write and flush
    if (fwrite(page->ptr_page, PAGE_SIZE, 1, g_db_file) != PAGE_SIZE) {
        return -1;
    }
    fflush(g_db_file);

    return 0;
}

/* Make given number of new free pages.
 * If success, return 0. Otherwise, return -1.
 */
int make_free_pages(int num_free_pages) {
    // Modify header info.
    off_t prev_free_page_offset = HEADER(g_header_page)->free_page_offset;

    HEADER(g_header_page)->free_page_offset = HEADER(g_header_page)->number_of_pages * PAGE_SIZE;
    HEADER(g_header_page)->number_of_pages += num_free_pages;

    if (write_page(g_header_page)) {
        HEADER(g_header_page)->free_page_offset = prev_free_page_offset;
        HEADER(g_header_page)->number_of_pages -= num_free_pages;
        return -1;
    }
    
    for (int i = 1; i <= num_free_pages; i++) {
        FreePage new_free_page;
        new_free_page.next_free_page_offset =
              i == num_free_pages ?     // is last page?
              (HEADER(g_header_page)->number_of_pages + i) * PAGE_SIZE :
              prev_free_page_offset ;
        
        Page wrapper = { &new_free_page, (HEADER(g_header_page)->number_of_pages + i - 1) * PAGE_SIZE };
        write_page(&wrapper);
    }

    return 0;
}

/* Get new free page from free page list.
 * If success, return pointer to the page. Otherwise, return NULL.
 */
Page * get_free_page(void) {
    if (!HEADER(g_header_page)->free_page_offset) {
        make_free_pages(10);
    }

    Page * new_free_page = read_page(HEADER(g_header_page)->free_page_offset);
    if (!new_free_page) return NULL;

    HEADER(g_header_page)->free_page_offset = FREE(new_free_page)->next_free_page_offset;
    if (write_page(g_header_page)) {
        HEADER(g_header_page)->free_page_offset = new_free_page->offset;
        return NULL;
    }

    return new_free_page;
}

/* Get new page from free page list. (except header page)
 * If success, return pointer to the page. Otherwise, return NULL.
 */
Page * get_new_page(PAGE_TYPE type) {
    Page * new_page;

    // Header page.
    if (type == HEADER_PAGE) {
        new_page           = calloc(1, sizeof(Page));
        new_page->ptr_page = calloc(1, sizeof(HeaderPage));
        new_page->offset   = 0;

        HEADER(new_page)->free_page_offset = 0;
        HEADER(new_page)->root_page_offset = 0;
        HEADER(new_page)->number_of_pages  = 1;

    // Free page.
    } else if (type == FREE_PAGE) {
        new_page = get_free_page();
        
    // Leaf page or internal page.
    } else {
        new_page = get_free_page();
        if (!new_page) return NULL;
        LEAF(new_page)->header.is_leaf = (type == LEAF_PAGE);
        LEAF(new_page)->header.number_of_keys = 0;
    }

    return new_page;
}