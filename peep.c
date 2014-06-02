#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/stat.h>
#include <sys/vt.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINES     UCHAR_MAX
#define MAX_COLUMNS   UCHAR_MAX

struct console {
  int vfd;                      /* fd of vcsa device */
  int tfd;                      /* fd of tty device */
  int printed;
  struct console_attributes {
    unsigned char lines;
    unsigned char columns;
    unsigned char x;
    unsigned char y;
  } attributes;
  struct {
    unsigned printed:1;
    unsigned light:1;
    unsigned plain:1;
    unsigned watch:1;
  } flags;
  unsigned short mask;
  unsigned short chars[MAX_LINES][MAX_COLUMNS];
};

static void console_init (struct console *cnsl);
static void console_open (struct console *cnsl, int num);
static void console_close (struct console *cnsl);
static void console_print (struct console *cnsl);
static void console_read (struct console *cnsl);

/**
 * Map the color number to ANSI foreground and background color values.
 */
static const char *ansi_fg_color[8] = {
  "30", "31", "32", "33", "34", "35", "36", "37"
};

static const char *ansi_bg_color[8] = {
  "40", "41", "42", "43", "44", "45", "46", "47"
};

static const int reverse_bits[8] = {
  0, 4, 2, 6, 1, 5, 3, 7
};

static void
console_init (struct console *cnsl)
{
  memset (cnsl, 0, sizeof (struct console));
}

static void
console_open (struct console *cnsl, int num)
{
  char vcs_path[32] = { 0 };
  char tty_path[32] = { 0 };

  /**
   * Accept numbers between 0 and 63 only. See vcs(4).
   */
  if (num & ~0x3f)
    err (EXIT_FAILURE, "Device number %d not in range [0,63].", num);

  snprintf (vcs_path, sizeof (vcs_path), "/dev/vcsa%d", num);
  snprintf (tty_path, sizeof (tty_path), "/dev/tty%d", num);

  cnsl->vfd = open (vcs_path, O_RDONLY);
  if (cnsl->vfd < 0)
    err (EXIT_FAILURE, "open(%s)", vcs_path);

  cnsl->tfd = open (tty_path, O_RDONLY);
  if (cnsl->tfd < 0)
    err (EXIT_FAILURE, "open(%s)", tty_path);

  cnsl->mask = 0;
  if (ioctl (cnsl->tfd, VT_GETHIFONTMASK, &cnsl->mask) != 0)
    err (EXIT_FAILURE, "ioctl(%s, VT_GETHIFONTMASK)", tty_path);
}

static void
console_read_core (struct console *cnsl, void *dst, size_t size)
{
  ssize_t ret;

  ret = read (cnsl->vfd, dst, size);
  if (ret < (ssize_t) size)
    err (EXIT_FAILURE, "read(%lu) == %ld", size, ret);
}

static void
console_read (struct console *cnsl)
{
  int i;

  /**
   * First read gives us the attributes (width, height, x, y), the following
   * reads the characters.
   */
  console_read_core (cnsl, &cnsl->attributes, sizeof (struct console_attributes));

  /**
   * No need to check if the previously read console size fits into buffer
   * since the buffer size is set to the data type's maximum value.
   */
  for (i = 0; i < cnsl->attributes.lines; i++)
    console_read_core (cnsl, cnsl->chars[i], cnsl->attributes.columns * sizeof (unsigned short));

  /**
   * Prepare for the next call.
   */
  if (cnsl->flags.watch)
    lseek (cnsl->vfd, SEEK_SET, 0);
}

static void
console_close (struct console *cnsl)
{
  close (cnsl->vfd);
  close (cnsl->tfd);
}

/**
 * Get the n bits of value v starting at position p.
 */
#define bits(v,p,n) \
  (((v) >> (p)) & ((1 << (n)) - 1))

static void
console_print_buffer (struct console *cnsl, char *buffer, size_t n, char attr)
{
  buffer[n] = '\0';

  if (cnsl->flags.plain) {
    /**
     * Plain output ignores trailing spaces. They don't look good in files.
     * We can't ignore them in non-plain output since they might be used
     * to display background colors.
     */
    for (; n > 0; n--) {
      if (buffer[n - 1] != ' ')
        break;
    }
    buffer[n] = '\0';

    printf ("%s", buffer);
  }
  else {
    const char *bold = "";
    const char *fg;
    const char *bg;

    fg = ansi_fg_color[reverse_bits[bits (attr, 0, 3)]];
    bg = ansi_bg_color[reverse_bits[bits (attr, 4, 3)]];

    if (bits (attr, 3, 1))
      bold = ";1";

    printf ("\x1b[%s;%s%sm%s\x1b[0m", fg, bg, bold, buffer);
  }
}

static void
console_print (struct console *cnsl)
{
  static char line[MAX_COLUMNS + 1];

  char data;
  char attr;
  char c;

  int i;
  int j;
  int p;

  /**
   * If this isn't the first call, go back to the beginning of the first line
   * to overwrite the previous output.
   */
  if (cnsl->flags.printed)
    printf ("\x1b[%dF", cnsl->attributes.lines);

  for (i = 0; i < cnsl->attributes.lines; i++) {
    p = 0;
    for (j = 0; j < cnsl->attributes.columns; j++) {
      data = (cnsl->chars[i][j] & 0xff);
      attr = (cnsl->chars[i][j] & ~cnsl->mask) >> 8;

      /**
       * If the font is a 512-character font, skip the 9th bit as well.
       */
      if (cnsl->mask)
        attr >>= 1;

      /**
       * If we print text attributes, we buffer text as long as it shares the
       * same attributes. Once the attribute changes, we print the buffered text
       * with the old attributes and start buffering the new one.
       *
       * If the output is plain, we just write the whole line into buffer and
       * then print the buffer once.
       */
      if (!cnsl->flags.plain) {
        if (j == 0) {
          c = attr;
        }
        else {
          if (attr != c) {
            console_print_buffer (cnsl, line, p, c);
            c = attr;
            p = 0;
          }
        }
      }
      line[p++] = data;
    }
    if (p > 0)
      console_print_buffer (cnsl, line, p, c);
    putchar ('\n');
  }
  cnsl->flags.printed = 1;
}

static void
usage (void)
{
  fprintf (stderr, "Usage: peep [-lpw] tty\n");
  exit (EXIT_FAILURE);
}

int
main (int argc, char **argv)
{
  struct console cnsl;
  char *tty;
  int ch;

  console_init (&cnsl);

  /**
   * Parse the options and arguments.
   *
   * The options are:
   *  -w (watch)  keep running and refresh every second.
   *  -l (light)  keep the current terminal's background / foreground color.
   *  -p (plain)  ignore all text attributes.
   */
  while ((ch = getopt (argc, argv, "lpw")) != -1) {
    switch (ch) {
      case 'l':
        ansi_fg_color[7] = "39";
        ansi_bg_color[0] = "49";
        cnsl.flags.light = 1;
        break;
      case 'p':
        cnsl.flags.plain = 1;
        break;
      case 'w':
        cnsl.flags.watch = 1;
        break;
      default:
        usage ();
    }
  }

  /**
   * Force plain (ANSI escape code free) output if we aren't writing
   * to a terminal. Also disable the watch option since it relies on
   * escape codes... watching is pointless without terminal output.
   */
  if (isatty (STDOUT_FILENO) == 0) {
    cnsl.flags.plain = 1;
    cnsl.flags.watch = 0;
  }

  argc -= optind;
  argv += optind;

  if (argc <= 0)
    usage ();

  tty = argv[0];
  while (*tty && !isdigit (*tty))
    tty++;

  if (*tty == '\0')
    err (EXIT_FAILURE, "invalid tty");

  console_open (&cnsl, atoi (tty));
  for (;;) {
    console_read (&cnsl);
    console_print (&cnsl);

    if (!cnsl.flags.watch)
      break;

    sleep (1);
  }
  console_close (&cnsl);
  return EXIT_SUCCESS;
}
