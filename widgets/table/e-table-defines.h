#ifndef __E_TABLE_DEFINES__
#define __E_TABLE_DEFINES__ 1

#define BUTTON_HEIGHT        10
#define BUTTON_PADDING       2
#define GROUP_INDENT         (BUTTON_HEIGHT + (BUTTON_PADDING * 2))

/* Padding above and below of the string in the header display */
#define HEADER_PADDING 2

#define MIN_ARROW_SIZE 10

typedef void (*ETableForeachFunc) (int model_row,
				   gpointer closure);

/* list selection modes */
typedef enum
{
	E_TABLE_CURSOR_LINE,
	E_TABLE_CURSOR_SIMPLE,
} ETableCursorMode;

#endif
