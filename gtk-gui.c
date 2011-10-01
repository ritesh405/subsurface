/* gtk-gui.c */
/* gtk UI implementation */
/* creates the window and overall layout
 * divelist, dive info, equipment and printing are handled in their own source files
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <gconf/gconf-client.h>

#include "dive.h"
#include "divelist.h"
#include "display.h"
#include "display-gtk.h"

#include "libdivecomputer.h"

GtkWidget *main_window;
GtkWidget *main_vbox;
GtkWidget *error_info_bar;
GtkWidget *error_label;
int        error_count;

#define DIVELIST_DEFAULT_FONT "Sans 8"
const char *divelist_font;

GConfClient *gconf;
struct units output_units;

#define GCONF_NAME(x) "/apps/subsurface/" #x

static GtkWidget *dive_profile;

visible_cols_t visible_cols = {TRUE, FALSE};

void repaint_dive(void)
{
	update_dive(current_dive);
	if (dive_profile)
		gtk_widget_queue_draw(dive_profile);
}

static char *existing_filename;

static void on_info_bar_response(GtkWidget *widget, gint response,
                                 gpointer data)
{
	if (response == GTK_RESPONSE_OK)
	{
		gtk_widget_destroy(widget);
		error_info_bar = NULL;
	}
}

void report_error(GError* error)
{
	if (error == NULL)
	{
		return;
	}
	
	if (error_info_bar == NULL)
	{
		error_count = 1;
		error_info_bar = gtk_info_bar_new_with_buttons(GTK_STOCK_OK,
		                                               GTK_RESPONSE_OK,
		                                               NULL);
		g_signal_connect(error_info_bar, "response", G_CALLBACK(on_info_bar_response), NULL);
		gtk_info_bar_set_message_type(GTK_INFO_BAR(error_info_bar),
		                              GTK_MESSAGE_ERROR);
		
		error_label = gtk_label_new(error->message);
		GtkWidget *container = gtk_info_bar_get_content_area(GTK_INFO_BAR(error_info_bar));
		gtk_container_add(GTK_CONTAINER(container), error_label);
		
		gtk_box_pack_start(GTK_BOX(main_vbox), error_info_bar, FALSE, FALSE, 0);
		gtk_widget_show_all(main_vbox);
	}
	else
	{
		error_count++;
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "Failed to open %i files.", error_count);
		gtk_label_set(GTK_LABEL(error_label), buffer);
	}
}

static void file_open(GtkWidget *w, gpointer data)
{
	GtkWidget *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_chooser_dialog_new("Open File",
		GTK_WINDOW(main_window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

	filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter, "*.xml");
	gtk_file_filter_add_pattern(filter, "*.XML");
	gtk_file_filter_add_mime_type(filter, "text/xml");
	gtk_file_filter_set_name(filter, "XML file");
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		GSList *filenames;
		char *filename;
		filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
		
		GError *error = NULL;
		while(filenames != NULL) {
			filename = (char *)filenames->data;
			parse_xml_file(filename, &error);
			if (error != NULL)
			{
				report_error(error);
				g_error_free(error);
				error = NULL;
			}
			
			g_free(filename);
			filenames = g_slist_next(filenames);
		}
		g_slist_free(filenames);
		report_dives();
		dive_list_update_dives();
	}
	gtk_widget_destroy(dialog);
}

static void file_save(GtkWidget *w, gpointer data)
{
	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new("Save File",
		GTK_WINDOW(main_window),
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
	if (!existing_filename) {
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "Untitled document");
	} else
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), existing_filename);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		save_dives(filename);
		g_free(filename);
		mark_divelist_changed(FALSE);
	}
	gtk_widget_destroy(dialog);
}

static void ask_save_changes()
{
	GtkWidget *dialog, *label, *content;
	dialog = gtk_dialog_new_with_buttons("Save Changes?",
		GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		NULL);
	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	label = gtk_label_new ("You have unsaved changes\nWould you like to save those before exiting the program?");
	gtk_container_add (GTK_CONTAINER (content), label);
	gtk_widget_show_all (dialog);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		file_save(NULL,NULL);
	}
	gtk_widget_destroy(dialog);
}

static gboolean on_delete(GtkWidget* w, gpointer data)
{
	/* Make sure to flush any modified dive data */
	update_dive(NULL);

	if (unsaved_changes())
		ask_save_changes();

	return FALSE; /* go ahead, kill the program, we're good now */
}

static void on_destroy(GtkWidget* w, gpointer data)
{
	gtk_main_quit();
}

static void quit(GtkWidget *w, gpointer data)
{
	/* Make sure to flush any modified dive data */
	update_dive(NULL);

	if (unsaved_changes())
		ask_save_changes();
	gtk_main_quit();
}

static void create_radio(GtkWidget *vbox, const char *name, ...)
{
	va_list args;
	GtkRadioButton *group = NULL;
	GtkWidget *box, *label;

	box = gtk_hbox_new(TRUE, 10);
	gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);

	label = gtk_label_new(name);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

	va_start(args, name);
	for (;;) {
		int enabled;
		const char *name;
		GtkWidget *button;
		void *callback_fn;

		name = va_arg(args, char *);
		if (!name)
			break;
		callback_fn = va_arg(args, void *);
		enabled = va_arg(args, int);

		button = gtk_radio_button_new_with_label_from_widget(group, name);
		group = GTK_RADIO_BUTTON(button);
		gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), enabled);
		g_signal_connect(button, "toggled", G_CALLBACK(callback_fn), NULL);
	}
	va_end(args);
}

#define UNITCALLBACK(name, type, value)				\
static void name(GtkWidget *w, gpointer data) 			\
{								\
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))	\
		menu_units.type = value;			\
}

static struct units menu_units;

UNITCALLBACK(set_meter, length, METERS)
UNITCALLBACK(set_feet, length, FEET)
UNITCALLBACK(set_bar, pressure, BAR)
UNITCALLBACK(set_psi, pressure, PSI)
UNITCALLBACK(set_liter, volume, LITER)
UNITCALLBACK(set_cuft, volume, CUFT)
UNITCALLBACK(set_celsius, temperature, CELSIUS)
UNITCALLBACK(set_fahrenheit, temperature, FAHRENHEIT)

#define OPTIONCALLBACK(name, option) \
static void name(GtkWidget *w, gpointer data) \
{ \
	option = GTK_TOGGLE_BUTTON(w)->active; \
}

OPTIONCALLBACK(otu_toggle, visible_cols.otu)
OPTIONCALLBACK(sac_toggle, visible_cols.sac)

static void preferences_dialog(GtkWidget *w, gpointer data)
{
	int result;
	GtkWidget *dialog, *font, *frame, *box, *vbox, *button;

	menu_units = output_units;

	dialog = gtk_dialog_new_with_buttons("Preferences",
		GTK_WINDOW(main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	frame = gtk_frame_new("Units");
	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 5);

	box = gtk_vbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(frame), box);

	create_radio(box, "Depth:",
		"Meter", set_meter, (output_units.length == METERS),
		"Feet",  set_feet, (output_units.length == FEET),
		NULL);

	create_radio(box, "Pressure:",
		"Bar", set_bar, (output_units.pressure == BAR),
		"PSI",  set_psi, (output_units.pressure == PSI),
		NULL);

	create_radio(box, "Volume:",
		"Liter",  set_liter, (output_units.volume == LITER),
		"CuFt", set_cuft, (output_units.volume == CUFT),
		NULL);

	create_radio(box, "Temperature:",
		"Celsius", set_celsius, (output_units.temperature == CELSIUS),
		"Fahrenheit",  set_fahrenheit, (output_units.temperature == FAHRENHEIT),
		NULL);

	frame = gtk_frame_new("Columns");
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), frame, FALSE, FALSE, 5);

	box = gtk_hbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(frame), box);

	button = gtk_check_button_new_with_label("Show SAC");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), visible_cols.sac);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 6);
	g_signal_connect(G_OBJECT(button), "toggled", G_CALLBACK(sac_toggle), NULL);

	button = gtk_check_button_new_with_label("Show OTU");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), visible_cols.otu);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 6);
	g_signal_connect(G_OBJECT(button), "toggled", G_CALLBACK(otu_toggle), NULL);

	font = gtk_font_button_new_with_font(divelist_font);
	gtk_box_pack_start(GTK_BOX(vbox), font, FALSE, FALSE, 5);

	gtk_widget_show_all(dialog);
	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result == GTK_RESPONSE_ACCEPT) {
		/* Make sure to flush any modified old dive data with old units */
		update_dive(NULL);

		divelist_font = strdup(gtk_font_button_get_font_name(GTK_FONT_BUTTON(font)));
		set_divelist_font(divelist_font);

		output_units = menu_units;
		update_dive_list_units();
		repaint_dive();
		update_dive_list_col_visibility();
		gconf_client_set_bool(gconf, GCONF_NAME(feet), output_units.length == FEET, NULL);
		gconf_client_set_bool(gconf, GCONF_NAME(psi), output_units.pressure == PSI, NULL);
		gconf_client_set_bool(gconf, GCONF_NAME(cuft), output_units.volume == CUFT, NULL);
		gconf_client_set_bool(gconf, GCONF_NAME(fahrenheit), output_units.temperature == FAHRENHEIT, NULL);
		gconf_client_set_bool(gconf, GCONF_NAME(SAC), ! visible_cols.sac, NULL); /* inverted to get the correct default */
		gconf_client_set_bool(gconf, GCONF_NAME(OTU), visible_cols.otu, NULL);
		gconf_client_set_string(gconf, GCONF_NAME(divelist_font), divelist_font, NULL);
	}
	gtk_widget_destroy(dialog);
}

static void renumber_dialog(GtkWidget *w, gpointer data)
{
	int result;
	GtkWidget *dialog, *frame, *button, *vbox;

	dialog = gtk_dialog_new_with_buttons("Renumber",
		GTK_WINDOW(main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	frame = gtk_frame_new("New starting number");
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 5);

	button = gtk_spin_button_new_with_range(1, 50000, 1);
	gtk_container_add(GTK_CONTAINER(frame), button);

	gtk_widget_show_all(dialog);
	result = gtk_dialog_run(GTK_DIALOG(dialog));
	if (result == GTK_RESPONSE_ACCEPT) {
		int nr = gtk_spin_button_get_value(GTK_SPIN_BUTTON(button));
		renumber_dives(nr);
		repaint_dive();
	}
	gtk_widget_destroy(dialog);
}

static void about_dialog(GtkWidget *w, gpointer data)
{
	const char *logo_property = NULL;
	GdkPixbuf *logo = NULL;
	GtkWidget *image = gtk_image_new_from_file("icon.svg");

	if (image) {
		logo = gtk_image_get_pixbuf(GTK_IMAGE(image));
		logo_property = "logo";
	}

	gtk_show_about_dialog(NULL,
		"program-name", "SubSurface",
		"comments", "Half-arsed divelog software in C",
		"license", "GPLv2",
		"version", VERSION_STRING,
		"copyright", "Linus Torvalds 2011",
		/* Must be last: */
		logo_property, logo,
		NULL);
}

static GtkActionEntry menu_items[] = {
	{ "FileMenuAction", GTK_STOCK_FILE, "File", NULL, NULL, NULL},
	{ "LogMenuAction",  GTK_STOCK_FILE, "Log", NULL, NULL, NULL},
	{ "HelpMenuAction", GTK_STOCK_HELP, "Help", NULL, NULL, NULL},
	{ "OpenFile",       GTK_STOCK_OPEN, NULL,   "<control>O", NULL, G_CALLBACK(file_open) },
	{ "SaveFile",       GTK_STOCK_SAVE, NULL,   "<control>S", NULL, G_CALLBACK(file_save) },
	{ "Print",          GTK_STOCK_PRINT, NULL,  "<control>P", NULL, G_CALLBACK(do_print) },
	{ "Import",         NULL, "Import", NULL, NULL, G_CALLBACK(import_dialog) },
	{ "Preferences",    NULL, "Preferences", NULL, NULL, G_CALLBACK(preferences_dialog) },
	{ "Renumber",       NULL, "Renumber", NULL, NULL, G_CALLBACK(renumber_dialog) },
	{ "Quit",           GTK_STOCK_QUIT, NULL,   "<control>Q", NULL, G_CALLBACK(quit) },
	{ "About",           GTK_STOCK_ABOUT, NULL,  NULL, NULL, G_CALLBACK(about_dialog) },
};
static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

static const gchar* ui_string = " \
	<ui> \
		<menubar name=\"MainMenu\"> \
			<menu name=\"FileMenu\" action=\"FileMenuAction\"> \
				<menuitem name=\"Open\" action=\"OpenFile\" /> \
				<menuitem name=\"Save\" action=\"SaveFile\" /> \
				<menuitem name=\"Print\" action=\"Print\" /> \
				<separator name=\"Separator1\"/> \
				<menuitem name=\"Import\" action=\"Import\" /> \
				<separator name=\"Separator2\"/> \
				<menuitem name=\"Preferences\" action=\"Preferences\" /> \
				<separator name=\"Separator3\"/> \
				<menuitem name=\"Quit\" action=\"Quit\" /> \
			</menu> \
			<menu name=\"LogMenu\" action=\"LogMenuAction\"> \
				<menuitem name=\"Renumber\" action=\"Renumber\" /> \
			</menu> \
			<menu name=\"Help\" action=\"HelpMenuAction\"> \
				<menuitem name=\"About\" action=\"About\" /> \
			</menu> \
		</menubar> \
	</ui> \
";

static GtkWidget *get_menubar_menu(GtkWidget *window)
{
	GtkActionGroup *action_group = gtk_action_group_new("Menu");
	gtk_action_group_add_actions(action_group, menu_items, nmenu_items, 0);

	GtkUIManager *ui_manager = gtk_ui_manager_new();
	gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);
	GError* error = 0;
	gtk_ui_manager_add_ui_from_string(GTK_UI_MANAGER(ui_manager), ui_string, -1, &error);

	gtk_window_add_accel_group(GTK_WINDOW(window), gtk_ui_manager_get_accel_group(ui_manager));
	GtkWidget* menu = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");

	return menu;
}

static void switch_page(GtkNotebook *notebook, gint arg1, gpointer user_data)
{
	repaint_dive();
}

static const char notebook_group[] = "123";
#define GRP_ID ((void *)notebook_group)
typedef struct {
	char *name;
	GtkWidget *widget;
	GtkWidget *box;
	gulong delete_handler;
	gulong destroy_handler;
} notebook_data_t;

static notebook_data_t nbd[2]; /* we rip at most two notebook pages off */

static GtkNotebook *create_new_notebook_window(GtkNotebook *source,
		GtkWidget *page,
		gint x, gint y, gpointer data)
{
	GtkWidget *win, *notebook, *vbox;
	notebook_data_t *nbdp;

	/* pick the right notebook page data and return if both are detached */
	if (nbd[0].widget == NULL)
		nbdp = nbd;
	else if (nbd[1].widget == NULL)
		nbdp = nbd + 1;
	else
		return NULL;

	nbdp->name = strdup(gtk_widget_get_name(page));
	nbdp->widget = win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), nbdp->name);
	gtk_window_move(GTK_WINDOW(win), x, y);

	/* Destroying the dive list will kill the application */
	nbdp->delete_handler = g_signal_connect(G_OBJECT(win), "delete-event", G_CALLBACK(on_delete), NULL);
	nbdp->destroy_handler = g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);

	nbdp->box = vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), vbox);

	notebook = gtk_notebook_new();
	gtk_notebook_set_group(GTK_NOTEBOOK(notebook), GRP_ID);
	gtk_widget_set_name(notebook, nbdp->name);
	/* disallow drop events */
	gtk_drag_dest_set(notebook, 0, NULL, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 6);
	gtk_widget_set_size_request(notebook, 450, 350);

	gtk_widget_show_all(win);
	return GTK_NOTEBOOK(notebook);
}

static void drag_cb(GtkWidget *widget, GdkDragContext *context,
	gint x, gint y, guint time,
	gpointer user_data)
{
	GtkWidget *source;
	notebook_data_t *nbdp;

	source = gtk_drag_get_source_widget(context);
	if (nbd[0].name && ! strcmp(nbd[0].name,gtk_widget_get_name(source)))
		nbdp = nbd;
	else if (nbd[1].name && ! strcmp(nbd[1].name,gtk_widget_get_name(source)))
		nbdp = nbd + 1;
	else
		/* HU? */
		return;

	gtk_drag_finish(context, TRUE, TRUE, time);

	/* we no longer need the widget - but getting rid of this is hard;
	 * remove the signal handler, remove the notebook from the box
	 * then destroy the widget (and clear out our data structure) */
	g_signal_handler_disconnect(nbdp->widget,nbdp->delete_handler);
	g_signal_handler_disconnect(nbdp->widget,nbdp->destroy_handler);
	gtk_container_remove(GTK_CONTAINER(nbdp->box), source);
	gtk_widget_destroy(nbdp->widget);
	nbdp->widget = NULL;
	free(nbdp->name);
	nbdp->name = NULL;
}

void init_ui(int argc, char **argv)
{
	GtkWidget *win;
	GtkWidget *notebook;
	GtkWidget *dive_info;
	GtkWidget *dive_list;
	GtkWidget *equipment;
	GtkWidget *menubar;
	GtkWidget *vbox;
	static const GtkTargetEntry notebook_target = {
		"GTK_NOTEBOOK_TAB", GTK_TARGET_SAME_APP, 0
	};

	gtk_init(&argc, &argv);

	g_type_init();
	gconf = gconf_client_get_default();

	if (gconf_client_get_bool(gconf, GCONF_NAME(feet), NULL))
		output_units.length = FEET;
	if (gconf_client_get_bool(gconf, GCONF_NAME(psi), NULL))
		output_units.pressure = PSI;
	if (gconf_client_get_bool(gconf, GCONF_NAME(cuft), NULL))
		output_units.volume = CUFT;
	if (gconf_client_get_bool(gconf, GCONF_NAME(fahrenheit), NULL))
		output_units.temperature = FAHRENHEIT;
	/* an unset key is FALSE - so in order to get the default behavior right we 
	   invert the meaning of the SAC key */
	visible_cols.otu = gconf_client_get_bool(gconf, GCONF_NAME(OTU), NULL);
	visible_cols.sac = ! gconf_client_get_bool(gconf, GCONF_NAME(SAC), NULL);
		
	divelist_font = gconf_client_get_string(gconf, GCONF_NAME(divelist_font), NULL);
	if (!divelist_font)
		divelist_font = DIVELIST_DEFAULT_FONT;

	error_info_bar = NULL;
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_icon_from_file(GTK_WINDOW(win), "icon.svg", NULL);
	g_signal_connect(G_OBJECT(win), "delete-event", G_CALLBACK(on_delete), NULL);
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(on_destroy), NULL);
	main_window = win;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), vbox);
	main_vbox = vbox;

	menubar = get_menubar_menu(win);
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

	/* Notebook for dive info vs profile vs .. */
	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 6);
	gtk_notebook_set_group(GTK_NOTEBOOK(notebook), GRP_ID);
	g_signal_connect(notebook, "create-window", G_CALLBACK(create_new_notebook_window), NULL);
	gtk_drag_dest_set(notebook, GTK_DEST_DEFAULT_ALL, &notebook_target, 1, GDK_ACTION_MOVE);
	g_signal_connect(notebook, "drag-drop", G_CALLBACK(drag_cb), notebook);
	g_signal_connect(notebook, "switch-page", G_CALLBACK(switch_page), NULL);

	/* Create the actual divelist */
	dive_list = dive_list_create();
	gtk_widget_set_name(dive_list, "Dive List");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dive_list, gtk_label_new("Dive List"));
	gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), dive_list, 1);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), dive_list, 1);

	/* Frame for dive profile */
	dive_profile = dive_profile_widget();
	gtk_widget_set_name(dive_profile, "Dive Profile");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dive_profile, gtk_label_new("Dive Profile"));
	gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), dive_profile, 1);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), dive_profile, 1);

	/* Frame for extended dive info */
	dive_info = extended_dive_info_widget();
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dive_info, gtk_label_new("Dive Notes"));

	/* Frame for dive equipment */
	equipment = equipment_widget();
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), equipment, gtk_label_new("Equipment"));

	gtk_widget_set_app_paintable(win, TRUE);
	gtk_widget_show_all(win);

	return;
}

void run_ui(void)
{
	gtk_main();
}

/* get the filenames the user selects and call the parsing function
 * on them
 * return 0 if the user cancelled the dialog
 */
int open_import_file_dialog(char *filterpattern, char *filtertext, 
			void(* parse_function)(char *))
{
	int ret=0;

	GtkWidget *dialog;
	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, filterpattern);
	gtk_file_filter_set_name(filter, filtertext);
	dialog = gtk_file_chooser_dialog_new("Open File",
					GTK_WINDOW(main_window),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		GSList *filenames;
		char *filename;
		filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
		while(filenames != NULL) {
			filename = (char *)filenames->data;
			parse_function(filename);
			g_free(filename);
			filenames = g_slist_next(filenames);
		}
		g_slist_free(filenames);
		ret = 1;
	}
	gtk_widget_destroy(dialog);

	return ret;
}

static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	struct dive *dive = current_dive;
	struct graphics_context gc = { .printer = 0 };
	int w,h;

	w = widget->allocation.width;
	h = widget->allocation.height;

	gc.cr = gdk_cairo_create(widget->window);
	set_source_rgb(&gc, 0, 0, 0);
	cairo_paint(gc.cr);

	if (dive)
		plot(&gc, w, h, dive);

	cairo_destroy(gc.cr);

	return FALSE;
}

GtkWidget *dive_profile_widget(void)
{
	GtkWidget *da;

	da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 350, 250);
	g_signal_connect(da, "expose_event", G_CALLBACK(expose_event), NULL);

	return da;
}

int process_ui_events(void)
{
	int ret=0;

	while (gtk_events_pending()) {
		if (gtk_main_iteration_do(0)) {
			ret = 1;
			break;
		}
	}
	return(ret);
}


static void fill_computer_list(GtkListStore *store)
{
	GtkTreeIter iter;
	struct device_list *list = device_list;

	for (list = device_list ; list->name ; list++) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
			0, list->name,
			1, list->type,
			-1);
	}
}

static GtkComboBox *dive_computer_selector(GtkWidget *vbox)
{
	GtkWidget *hbox, *combo_box, *frame;
	GtkListStore *model;
	GtkCellRenderer *renderer;

	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 3);

	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	fill_computer_list(model);

	frame = gtk_frame_new("Dive computer");
	gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, TRUE, 3);

	combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(model));
	gtk_container_add(GTK_CONTAINER(frame), combo_box);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_box), renderer, "text", 0, NULL);

	return GTK_COMBO_BOX(combo_box);
}

static GtkEntry *dive_computer_device(GtkWidget *vbox)
{
	GtkWidget *hbox, *entry, *frame;

	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 3);

	frame = gtk_frame_new("Device name");
	gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, TRUE, 3);

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame), entry);
	gtk_entry_set_text(GTK_ENTRY(entry), "/dev/ttyUSB0");

	return GTK_ENTRY(entry);
}

void import_dialog(GtkWidget *w, gpointer data)
{
	int result;
	GtkWidget *dialog, *hbox, *vbox;
	GtkComboBox *computer;
	GtkEntry *device;
	device_data_t devicedata = {
		.devname = NULL,
	};

	dialog = gtk_dialog_new_with_buttons("Import from dive computer",
		GTK_WINDOW(main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	computer = dive_computer_selector(vbox);
	device = dive_computer_device(vbox);

	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 3);
	devicedata.progress.bar = gtk_progress_bar_new();
	gtk_container_add(GTK_CONTAINER(hbox), devicedata.progress.bar);

	gtk_widget_show_all(dialog);
	result = gtk_dialog_run(GTK_DIALOG(dialog));
	switch (result) {
		int type;
		GtkTreeIter iter;
		GtkTreeModel *model;
		const char *comp;
	case GTK_RESPONSE_ACCEPT:
		if (!gtk_combo_box_get_active_iter(computer, &iter))
			break;
		model = gtk_combo_box_get_model(computer);
		gtk_tree_model_get(model, &iter,
			0, &comp,
			1, &type,
			-1);
		devicedata.type = type;
		devicedata.name = comp;
		devicedata.devname = gtk_entry_get_text(device);
		do_import(&devicedata);
		break;
	default:
		break;
	}
	gtk_widget_destroy(dialog);

	report_dives();
	dive_list_update_dives();
}

void update_progressbar(progressbar_t *progress, double value)
{
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress->bar), value);
}


void set_filename(const char *filename)
{
	if (filename)
		existing_filename = strdup(filename);
	return;
}