#include <dialog.h>

#define LLEN(n) ((n) * MENUBOX_TAGS)
#define ItemName(i)    items[LLEN(i)]
#define ItemText(i)    items[LLEN(i) + 1]
#define ItemHelp(i)    items[LLEN(i) + 2]

int
dlg_dummy_menutext(DIALOG_LISTITEM * items, int current, char *newtext);

int
my_dialog_menu(const char *title,
	    const char *cprompt,
	    int height,
	    int width,
	    int menu_height,
	    int item_no,
	    char **items)
{
    int result;
    int choice;
    int i;
    DIALOG_LISTITEM *listitems;

    listitems = dlg_calloc(DIALOG_LISTITEM, (size_t) item_no + 1);
    assert_ptr(listitems, "dialog_menu");

    for (i = 0; i < item_no; ++i) {
	listitems[i].name = ItemName(i);
	listitems[i].text = ItemText(i);
	listitems[i].help = ((dialog_vars.item_help)
			     ? ItemHelp(i)
			     : dlg_strempty());
    }
    dlg_align_columns(&listitems[0].text, sizeof(DIALOG_LISTITEM), item_no);

    result = dlg_menu(title,
		      cprompt,
		      height,
		      width,
		      menu_height,
		      item_no,
		      listitems,
		      &choice,
		      dlg_dummy_menutext);

    dlg_free_columns(&listitems[0].text, sizeof(DIALOG_LISTITEM), item_no);
    free(listitems);
    attrset(0); // restore fg, bg, attrs
    if (result) // dialog cancelled
        return -1;
    else // entry selected
        return choice;
}
