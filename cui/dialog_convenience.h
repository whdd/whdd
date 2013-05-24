#ifndef DIALOG_CONVENIENCE_H
#define DIALOG_CONVENIENCE_H

/*
 * dialog(3) dialog_menu is useless, as it tells to caller only the fact that
 * dialog was discarded, or an entry was selected. WHICH entry was selected,
 * is not propagated.
 *
 * "...was probably designed by a deranged monkey
 * on some serious mind-controlling substances" (C) Linus
 *
 * input params are same as of original dialog_menu()
 * @return -1 if dialog was cancelled by user,
 * list entry index otherwise.
 */
int
my_dialog_menu(const char *title,
	    const char *cprompt,
	    int height,
	    int width,
	    int menu_height,
	    int item_no,
	    char **items);

#endif // DIALOG_CONVENIENCE_H
