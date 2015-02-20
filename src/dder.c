/* dder.c */

/* Parse der files */

/* Sébastien Millet, February 2015 */

/*#define DEBUG*/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>


/*

DER Encoding information taken from:

	ITU-T Rec. X.690 (07/2002)
		Available here (PDF): http://www.itu.int/ITU-T/studygroups/com17/languages/X.690-0207.pdf

*/

const char *classes[] = {
	"Universal",        /* CLASS_UNIVERSAL */
	"Application",      /* CLASS_APPLICATION */
	"Context-specific", /* CLASS_CONTEXT_SPECIFIC */
	"Private"           /* CLASS_PRIVATE */
};
const char *short_classes[] = {
	"univ", /* CLASS_UNIVERSAL */
	"appl", /* CLASS_APPLICATION */
	"cont", /* CLASS_CONTEXT_SPECIFIC */
	"priv"  /* CLASS_PRIVATE */
};
#define TAG_CLASS_UNIVERSAL        0
#define TAG_CLASS_APPLICATION      1
#define TAG_CLASS_CONTEXT_SPECIFIC 2
#define TAG_CLASS_PRIVATE          3

const char *PCs[] = {
	"Primitive",  /* TAG_TYPE_PRIMITIVE */
	"Constructed" /* TAG_TYPE_CONSTRUCTED */
};
const char *short_PCs[] = {
	"prim", /* TAG_TYPE_PRIMITIVE */
	"cons"  /* TAG_TYPE_CONSTRUCTED */
};
#define TAG_TYPE_PRIMITIVE   0
#define TAG_TYPE_CONSTRUCTED 1

struct t_tag_universal_class {
	const char *name;
	int pc;
};

struct t_tag_universal_class universal_class_tags[] = {
	{"EOC", 0},
	{"BOOLEAN", 0},
	{"INTEGER", 0},
	{"BIT STRING", 2},
	{"OCTET STRING", 2},
	{"NULL", 0},
	{"OBJECT IDENTIFIER", 0}, /* TAG_U_OBJECT_IDENTIFIER */
	{"OBJECT DESCRIPTOR", 2},
	{"EXTERNAL", 1},
	{"REAL", 0},
	{"ENUMERATED", 0},
	{"EMBEDDED PDV", 1},
	{"UTF8String", 2},
	{"RELATIVE-OID", 1},
	{"(reserved)", -1},
	{"(reserved)", -1},
	{"SEQUENCE", 1},
	{"SET", 1},
	{"NUMERICSTRING", 2},
	{"PRINTABLESTRING", 2},
	{"T61STRING", 2},
	{"VIDEOTEXSTRING", 2},
	{"IA5String", 2},
	{"UTCTime", 2},
	{"GeneralizedTime", 2},
	{"GraphicString", 2},
	{"VisibleString", 2},
	{"GeneralString", 2},
	{"UniversalString", 2},
	{"CHARACTER STRING", 2},
	{"BMPString", 2},
	{"(long form)", -1}       /* TAG_U_LONG_FORMAT */
};
#define TAG_U_OBJECT_IDENTIFIER 6
#define TAG_U_LONG_FORMAT       31

const char *length_coding_types[] = {
	"ONE BYTE",   /* LENGTH_CODE_TYPE_ONE */
	"MULTI BYTES" /* LENGTH_CODA_TYPE_MULTI */
};
#define LENGTH_CODE_TYPE_ONE   0
#define LENGTH_CODA_TYPE_MULTI 1


/*

Program options from the command line

*/

	/* OL stands for Out Level (no link with Olympic Lyonnais) */
typedef enum {OL_NORMAL, OL_VERBOSE, OL_VERYVERBOSE} out_level_t;
out_level_t opt_ol = OL_NORMAL;
int opt_hex = 1;
long unsigned opt_width = 16;


/*

========================================================================

*/

#define UNUSED(x) (void)(x)

#define assert(b) \
if (!(b)) { \
	out_err("FATAL ERROR: file %s, line %i\n", __FILE__, __LINE__); \
	exit(-999); \
}

int out(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int r = vprintf(fmt, args);
	va_end(args);
	return r;
}

int out_err(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int r = vfprintf(stderr, fmt, args);
	va_end(args);
	return r;
}

void out_errno(const ssize_t offset)
{
	if (offset >= 0)
		out_err("Error: %s, position %u\n", strerror(errno), (unsigned int)offset);
	else
		out_err("Error: %s\n", strerror(errno));
}

#ifdef DEBUG
#define out_dbg(args...) \
	out(args)
#else
#define out_dbg(args...) ;
#endif

void usage()
{
	out_err("Usage:\n");
	out_err("  dder [OPTION]... [FILE]\n");
	out_err("Parses FILE according to DER syntax\n");
	out_err("Uses standard input if FILE is not specified\n");
	out_err("  -help      print this help screen\n");
	out_err("  -version   output version information and exit\n");
	out_err("  -v         verbose output. Implies -hex\n");
	out_err("  -hex       hexadecimal output of data values\n");
	out_err("  -width     number of bytes per line (hexadecimal output)\n");
	out_err("  --         end of parameters\n");
	exit(-1);
}

void version()
{
	out("dder version 0.1\n");
	exit(0);
}

ssize_t get_file_size(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

char *s_strncpy(char *dest, const char *src, size_t n)
{
		strncpy(dest, src, n);
		dest[n - 1] = '\0';
		return dest;
}
	/* The define below triggers an error if usual strncpy is used */
#define strncpy(a, b, c) ErrorDontUse_strncpy_Use_s_strncpy_Instead

char *s_strncat(char *dest, const char *src, size_t n)
{
	strncat(dest, src, n);
	dest[n - 1] = '\0';
	return dest;
}
	/* The define below triggers an error if usual strncat is used */
#define strncat(a, b, c) ErrorDontUse_strncat_Use_s_strncat_Instead

char *char_8bit_to_bin_str(char *b, const size_t blen, unsigned char c)
{
		/* Yes, you should invoke this function for 8-bit characters! */
	assert(blen >= 9);

	int i;
	for (i = (int)(blen >= 9 ? 7 : blen - 2); i >= 0; --i) {
		b[i] = (c % 2 == 0 ? '0' : '1');
		c >>= 1;
	}
	b[blen >= 9 ? 8 : blen - 1] = '\0';
	return b;
}

void myfclose(FILE **F, const char *err, size_t position)
{
	if (err != NULL)
		out_err("Error: %s, position: %u\n", err, (unsigned int)position);
	if (*F != stdin)
		fclose(*F);
	*F = NULL;
}

int myfgetc(FILE **F, size_t *offset)
{
	if (*F == NULL)
		return EOF;

	++(*offset);
	int r = fgetc(*F);
	if (r == EOF)
		myfclose(F, "unexpected end of file", *offset - 1);
	return r;
}

void out_sequence(size_t offset, char *buf, const unsigned long len, const int is_value)
{

	const unsigned long hbytes = opt_width / 2;

	char *str = malloc(opt_width + 50);
	int strpos = 0;

	if (!is_value || opt_hex) {
		unsigned long i;
		unsigned long lim = ((len + opt_width - 1) / opt_width) * opt_width;
		if (!lim)
			lim = (unsigned long)opt_width;
		for (i = 0; i < lim; ++i) {
			if (!(i % opt_width)) {
				strpos = 0;
				if (len)
					out("%06x  ", (unsigned int)offset);
				else
					out("        ");
			}
			if (!((i + hbytes) % opt_width)) {
				out("   ");
				str[strpos++] = ' ';
				str[strpos++] = ' ';
			}
			if (i < len) {
				out("%02x ", (unsigned char)buf[i]);
				str[strpos++] = (buf[i] < ' ' || buf[i] == 127 ? '.' : buf[i]);
			} else {
				out("   ");
				str[strpos++] = ' ';
			}
			if (!((i + 1) % opt_width)) {
				out("  ");
				if (is_value) {
					str[strpos] = '\0';
					out("%s ", str);
				}
				if (i + 1 < lim) {
					out("\n");
					offset += opt_width;
				}
			}
		}
	} else {
		out("%06x  ", (unsigned int)offset);
		char c;
		unsigned i;
		for (i = 0; i < len; ++i) {
			c = buf[i];
			if (c < ' ' || c == 127)
				c = '.';
			out("%c", c);
		}
		out("  ");
	}
	free(str);
}

int check_position(FILE **F, const size_t offset, const ssize_t remaining_length, const int insert_newline)
{
	if (*F == NULL)
		return 0;
	if (remaining_length < 0) {
		if (insert_newline)
			out("\n");
		myfclose(F, "inconsistent items length", offset);
		return 0;
	}
	return 1;
}

void get_tag_name(char *s, const size_t slen, const int tag_class, const int tag_number)
{
	if (tag_class == TAG_CLASS_UNIVERSAL) {
		s_strncpy(s, universal_class_tags[tag_number].name, slen);
		s[slen - 1] = '\0';
	} else {
		snprintf(s, slen, "[ %i ]", tag_number);
	}
}

	/* Includes the initial byte of number 31 */
#define TAG_U_LONG_FORMAT_MAX_BYTES 6
	/* Includes the initial byte that indicates the number of bytes used to encode length */
#define LENGTH_MULTIBYTES_MAX_BYTES 7

int parse_identifier_length(FILE **F, size_t *offset, ssize_t *remaining_length, int *tag_class, int *tag_PC, int *tag_number, unsigned long *length)
{
	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(F, offset)) == EOF)
		return 0;

	*tag_class = (c & 0xc0) >> 6;
	*tag_PC = (c & 0x20) >> 5;
	*tag_number = (c & 0x1F);
	int allowed_pc = universal_class_tags[*tag_number].pc;
	if (tag_class == TAG_CLASS_UNIVERSAL && allowed_pc >= 0 && allowed_pc <= 1 && *tag_PC != allowed_pc) {
		*tag_PC = TAG_TYPE_PRIMITIVE;
		out_err("Warning: tag number does not match primitive/constructed bit\n");
	}
	char tag_name[100];
	get_tag_name(tag_name, sizeof(tag_name), *tag_class, *tag_number);

	char buf[TAG_U_LONG_FORMAT_MAX_BYTES + LENGTH_MULTIBYTES_MAX_BYTES];

	buf[0] = (char)c;
	if (opt_ol >= OL_VERBOSE)
		out_sequence(old_offset, buf, 1, 0);

	if (opt_ol == OL_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)c);
		out("%c%c %c %c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0);
		out("^^         Tag class:  %3i -> %s\n", *tag_class, classes[*tag_class]);
		out_sequence(0, NULL, 0, 0);
		out("   ^       Tag type:   %3i -> %s\n", *tag_PC, PCs[*tag_PC]);
		out_sequence(0, NULL, 0, 0);
		out("     ^^^^^ Tag number: %3i -> %s\n", *tag_number, tag_name);
	} else if (opt_ol >= OL_VERBOSE) {
		out("%s-%s: %s\n", short_classes[*tag_class], short_PCs[*tag_PC], tag_name);
	}

	int pos = 0;

	if (*tag_number == TAG_U_LONG_FORMAT) {
		pos = 1;

		old_offset = *offset;
		do {
			if ((c = myfgetc(F, offset)) == EOF)
				return 0;
			buf[pos] = (char)c;
			--(*remaining_length);
		} while (buf[pos] & 0x80 && ++pos < TAG_U_LONG_FORMAT_MAX_BYTES);
		if (pos == sizeof(buf)) {
			myfclose(F, "Tag number too big", old_offset);
			return 0;
		}
		int rev;
		long unsigned multi = 1;
		int shift = 0;
		unsigned rmask;
		unsigned lmask;
		unsigned bm1;
		unsigned v0;
		long unsigned value = 0;
		for (rev = pos; rev >= 1; --rev) {
			if (rev == 1)
				bm1 = 0;
			else
				bm1 = (unsigned)buf[rev - 1];
			rmask = (0x7Fu >> shift);
			lmask = (0xFFu << (7 - shift)) & 0xFFu;
			v0 = (long unsigned)(((bm1 << (7 - shift)) & lmask) | (((unsigned)buf[rev] >> shift) & rmask));

			value += v0 * multi;
			multi *= 256;   /* Can be written <<8, but... */
			++shift;
		}
		*tag_number = (int)value;
		if (opt_ol >= OL_VERBOSE) {
			out_sequence(old_offset, buf + 1, (unsigned long)pos, 0);
			out("Tag number: %i\n", *tag_number);
		}
	}

	--(*remaining_length);

	if (!check_position(F, *offset, *remaining_length, 0))
		return 0;

/*  char bl[LENGTH_MULTIBYTES_MAX_BYTES];*/
	char *bl = buf + pos + 1;

	int cc;
	size_t old_loffset = *offset;
	if ((cc = myfgetc(F, offset)) == EOF)
		return 0;
	int n = 0;
	bl[0] = (char)cc;
	*length = (unsigned long)cc;
	int length_type = ((cc & 0x80) >> 7);
	if (cc & 0x80) {
		n = (cc & 0x7F);
		if (n > LENGTH_MULTIBYTES_MAX_BYTES - 1) {
			myfclose(F, "number of bytes to encode length exceeds maximum", old_loffset);
			return 0;
		} else if (n == 0) {
			myfclose(F, "number of bytes to encode length cannot be null", old_loffset);
			return 0;
		}
		*length = 0;
		int i;
		for (i = 1; i <= n; ++i) {
			if ((cc = myfgetc(F, offset)) == EOF)
				return 0;
			(*length) <<= 8;
			(*length) += (unsigned int)cc;
			bl[i] = (char)cc;
		}
	}
	if (opt_ol >= OL_VERBOSE)
		out_sequence(old_loffset, bl, (unsigned long)n + 1, 0);

	if (opt_ol == OL_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)(bl[0]));
		out("%c %c%c%c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0);
		out("^          Len type: %i -> %s\n", length_type, length_coding_types[length_type]);
		out_sequence(0, NULL, 0, 0);
		if (length_type == LENGTH_CODE_TYPE_ONE) {
			out("  ^^^^^^^  Len: %lu", *length);
		} else {
			out("  ^^^^^^^  Len nb bytes: %i, value: %lu", n, *length);
		}
		out("\n");
	} else if (opt_ol >= OL_VERBOSE) {
		out("Length: %lu\n", *length);
	}

	if (opt_ol == OL_NORMAL) {
		out_sequence(old_offset, buf, (size_t)(pos + n + 2), 0);
		out("%s-%s: %s, len: %lu\n", short_classes[*tag_class], short_PCs[*tag_PC], tag_name, *length);
	}

	*remaining_length -= (n + 1);

	return check_position(F, *offset, *remaining_length, 0);
}

int decode_oid(char *p, const size_t plen, const char *buf, const size_t buflen)
{
	char tmp[20];
	int pos = 0;
	while ((unsigned)pos < buflen) {
		int old_pos = pos;
		for (; (buf[pos] & 0x80) && (unsigned)pos < buflen; ++pos)
			;
		if ((unsigned)pos >= buflen) {
			return 0;
		}
		int rev;
		long unsigned multi = 1;
		int shift = 0;
		unsigned rmask;
		unsigned lmask;
		unsigned bm1;
		unsigned v0;
		long unsigned value = 0;
		for (rev = pos; rev >= old_pos; --rev) {
			if (rev == old_pos)
				bm1 = 0;
			else
				bm1 = (unsigned)buf[rev - 1];
			rmask = (0x7Fu >> shift);
			lmask = (0xFFu << (7 - shift)) & 0xFFu;
			v0 = (long unsigned)(((bm1 << (7 - shift)) & lmask) | (((unsigned)buf[rev] >> shift) & rmask));

			value += v0 * multi;
			multi *= 256;   /* Can be written <<8, but... */
			++shift;
		}

		if (!old_pos) {
			int x = (int)value / 40;
			if (x > 2)
				x = 2;
			int y = (int)value - 40 * x;
			snprintf(p, plen, "%i.%i", x, y);
		} else {
			snprintf(tmp, sizeof(tmp), ".%lu", value);
			s_strncat(p, tmp, plen);
		}
		++pos;
	}
	return 1;
}

void parse(FILE **F, size_t *offset, ssize_t *remaining_length)
{
	int tag_class;
	int tag_PC;
	int tag_number;
	unsigned long length;

	if (!parse_identifier_length(F, offset, remaining_length, &tag_class, &tag_PC, &tag_number, &length))
		return;

	if (tag_PC == TAG_TYPE_CONSTRUCTED) {
		ssize_t inner_remaining_length = (ssize_t)length;
		while(inner_remaining_length >= 1 && *F != NULL) {
			parse(F, offset, &inner_remaining_length);
		}
		if (inner_remaining_length && *F != NULL) {
			myfclose(F, "contained data out of container boundaries", *offset);
			return;
		}
		*remaining_length -= (ssize_t)length;
	} else {
		char *buf;
		buf = (char *)malloc(length);
		size_t nbread;
		nbread = fread(buf, 1, (size_t)length, *F);
		size_t old_offset = *offset;
		*offset += nbread;
		*remaining_length -= (ssize_t)nbread;

		out_sequence(old_offset, buf, nbread, 1);

		if (nbread != length) {
			if (feof(*F)) {
				out("\n");
				myfclose(F, "unexpected end of file", *offset);
			} else {
				out_errno((ssize_t)*offset);
				myfclose(F, NULL, 0);
			}
		} else {
			if (!check_position(F, *offset, *remaining_length, 1)) {
				free(buf);
				return;
			}
			if (tag_class == TAG_CLASS_UNIVERSAL && tag_number == TAG_U_OBJECT_IDENTIFIER) {
				const size_t sizeof_printable = 200;
				char *printable = malloc(sizeof_printable);
				if (!decode_oid(printable, sizeof_printable, buf, length)) {
					out("\n");
					myfclose(F, "unable to decode OID", *offset);
				} else {
					if (opt_ol == OL_VERYVERBOSE)
						out("OID: %s", printable);
					else
						out("\n        OID: %s", printable);
				}
				free(printable);
			}
		}
		if (*F == NULL) {
			free(buf);
			return;
		}
		free(buf);
		out("\n");
	}
	return;
}

void opt_check(int n, const char *opt)
{
	static int defined_options[4] = {0, 0, 0};

	if (defined_options[n]) {
		out_err("Option %s already set\n", opt);
		exit(-2);
	} else
		defined_options[n] = 1;
}

int main(int argc, char **argv)
{
	FILE *F = NULL;

	int optset_verbose = 0;
	int optset_veryverbose = 0;
	int optset_text = 0;

	int a = 1;
	while (a >= 1 && a < argc) {
		if (!strcasecmp(argv[a], "-help")) {
			usage();
		} else if (!strcasecmp(argv[a], "-version")) {
			version();
		} else if (!strcasecmp(argv[a], "-verbose")) {
			opt_check(0, argv[a]);
			optset_verbose = 1;
			opt_ol = OL_VERBOSE;
		} else if (!strcasecmp(argv[a], "-veryverbose")) {
			opt_check(1, argv[a]);
			optset_veryverbose = 1;
			opt_ol = OL_VERYVERBOSE;
			opt_hex = 1;
		} else if (!strcasecmp(argv[a], "-text")) {
			opt_check(2, argv[a]);
			optset_text = 1;
			opt_hex = 0;
		} else if (!strcasecmp(argv[a], "-width")) {
			opt_check(3, argv[a]);
			if (++a >= argc) {
				a = -(a - 1);
			} else {
				opt_width = (unsigned long)atoi(argv[a]);
				if (opt_width < 2) {
					out_err("Width must be greater than, or equal to, 2\n");
					a = 0;
				} else if (opt_width % 2) {
					out_err("Width must be an even number\n");
					a = 0;
				}
			}
		} else if (argv[a][0] == '-') {
			if (strcmp(argv[a], "--")) {
				out_err("Unknown option %s\n", argv[a]);
				a = 0;
			} else {
				++a;
				break;
			}
		} else {
			break;
		}
		if (a >= 1)
			++a;
	}
	if (a <= -1) {
		out_err("Option %s: missing value\n", argv[-a]);
	} else if (a == 0) {
		;
	} else if (a < argc - 1) {
		out_err("Trailing parameter(s)\n");
		a = 0;
	} else if (optset_veryverbose && optset_text) {
		out_err("incompatible options -veryverbose and -text\n");
		a = 0;
	} else if (optset_verbose && optset_veryverbose) {
		out_err("incompatible options -veryverbose and -verbose\n");
		a = 0;
	} else if (a >= argc) {
		out("Reading from standard input...\n");
		F = stdin;
	}
	if (a <= 0)
		usage();

	ssize_t s = INT_MAX;
	if (F == NULL) {
		char *f = argv[a];

		s = get_file_size(f);
		if (s < 0) {
			out_errno(-1);
			exit(-3);
		}
		if ((F = fopen(f, "rb")) == NULL) {
			out_errno(-1);
			exit(-2);
		}

		out("Reading from file %s...\n", f);
	}

	size_t offset = 0;
	parse(&F, &offset, &s);
	if (F != NULL && F != stdin) {
		if (s != 0 || fgetc(F) != EOF) {
			out_dbg("s = %li\n", s);
			myfclose(&F, "trailing characters in file", offset);
			return -5;
		} else {
			myfclose(&F, NULL, 0);
		}
	} else {
		return -99;
	}
	return 0;
}

