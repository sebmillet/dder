/* dder.c */

/* Parse der files */

/* Sébastien Millet, February 2015 */

/*#define DEBUG*/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAS_LIB_OPENSSL
#include <openssl/objects.h>
#endif

#ifdef _WIN32
#define snprintf _snprintf
#define strcasecmp _stricmp
#endif

#define FALSE	0
#define TRUE	1

/*

DER Encoding information taken from:

	ITU-T Rec. X.690 (07/2002)
		Available here (PDF): http://www.itu.int/ITU-T/studygroups/com17/languages/X.690-0207.pdf

*/

	/* Includes the initial byte of tag number 31 */
#define TAG_U_LONG_FORMAT_MAX_BYTES 6
	/* Includes the initial byte that indicates the number of bytes used to encode length */
#define LENGTH_MULTIBYTES_MAX_BYTES 7

typedef struct {
	int class;
	int p_or_c;
	int number;
	unsigned long length;
	char hbytes[TAG_U_LONG_FORMAT_MAX_BYTES + LENGTH_MULTIBYTES_MAX_BYTES + 1];
	int hlen;
} taglength_t;

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
		/* Primitive or Constructed? => 0 for P only, 1 for C only, */
		/* 2 for P or C, -1 for non applicable */
	int type;
	int is_string;
};

struct t_tag_universal_class universal_class_tags[] = {
	{"EOC", 0, 0},
	{"BOOLEAN", 0, 0},
	{"INTEGER", 0, 0},
	{"BIT STRING", 2, 0},
	{"OCTET STRING", 2, 0},
	{"NULL", 0, 0},
	{"OBJECT IDENTIFIER", 0, 0}, /* TAG_U_OBJECT_IDENTIFIER */
	{"OBJECT DESCRIPTOR", 2, 0},
	{"EXTERNAL", 1, 0},
	{"REAL", 0, 0},
	{"ENUMERATED", 0, 0},
	{"EMBEDDED PDV", 1, 0},
	{"UTF8String", 2, 1},
	{"RELATIVE-OID", 1, 0},
	{"(reserved)", -1, 0},
	{"(reserved)", -1, 0},
	{"SEQUENCE", 1, 0},
	{"SET", 1, 0},
	{"NUMERICSTRING", 2, 0},
	{"PRINTABLESTRING", 2, 1},
	{"T61STRING", 2, 1},
	{"VIDEOTEXSTRING", 2, 1},
	{"IA5String", 2, 1},
	{"UTCTime", 2, 0},
	{"GeneralizedTime", 2, 0},
	{"GraphicString", 2, 1},
	{"VisibleString", 2, 1},
	{"GeneralString", 2, 1},
	{"UniversalString", 2, 1},
	{"CHARACTER STRING", 2, 1},
	{"BMPString", 2, 1},
	{"(long form)", -1, 0}       /* TAG_U_LONG_FORMAT */
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

	/* 200, c'est déjà pas mal ! */
#define MAX_RECURSIVE_LEVELS 200

	/* OL stands for Out Level (no link with Olympic Lyonnais) */
typedef enum {OL_NORMAL = 0, OL_VERBOSE = 1, OL_VERYVERBOSE = 2} out_level_t;
out_level_t opt_ol = OL_NORMAL;

	/* OD stands for Out Data */
typedef enum {OD_UNDEF, OD_SMART, OD_ENFORCE_HEX, OD_ENFORCE_TEXT} out_data_t;
out_data_t opt_od = OD_SMART;

	/* Nb bytes output on each line. MUST be an even number */
long unsigned opt_width = 16;

	/* Prefix to add as many times as current recursive level */
char opt_recursive_pattern[100];


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

#define ssize_t long signed

void out_errno(const ssize_t offset)
{
	if (offset >= 0)
		out_err("Error: %s, position %u\n", strerror(errno), (unsigned int)offset);
	else
		out_err("Error: %s\n", strerror(errno));
}

#ifdef DEBUG
#define out_dbg(...) \
	out(__VA_ARGS__)
#else
#define out_dbg(...) ;
#endif

void usage()
{
	out_err("Usage:\n");
	out_err("  dder [OPTION]... [FILE]\n");
	out_err("Parses FILE according to DER syntax\n");
	out_err("Uses standard input if FILE is not specified\n");
	out_err("\n");
	out_err("FILE must be in der format. If it is in pem\n");
	out_err("format you can use an openssl command to convert\n");
	out_err("  If FILE is a private key:\n");
	out_err("  openssl pley -in FILE -outform der -out out.der\n");
	out_err("  If FILE is a certificate request:\n");
	out_err("  openssl req -in FILE -outform der -out out.der\n");
	out_err("  If FILE is a certificate:\n");
	out_err("  openssl x509 -in FILE -outform der -out out.der\n");
	out_err("\n");
	out_err("Options are case insensitive. Options list:\n");
	out_err("  -help        print this help screen\n");
	out_err("  -version     output version information and exit\n");
	out_err("  -verbose     verbose output.\n");
	out_err("  -veryverbose *very* verbose output. Implies -hex\n");
	out_err("  -text        enforce text output of data values\n");
	out_err("  -hex         enforce hex output of data values\n");
	out_err("  -width N     number of bytes per line, must be even\n");
	out_err("  -recursive S string to add at the beginning of output lines,\n");
	out_err("               one occurence of string for each level\n");
	out_err("  --           end of parameters, next option is a file name\n");
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

int myfgetc(FILE **F, size_t *offset, int loose_read)
{
	if (*F == NULL)
		return EOF;

	++(*offset);
	int r = fgetc(*F);
	if (r == EOF) {
		if (!loose_read)
			myfclose(F, "unexpected end of file", *offset - 1);
		else
			myfclose(F, NULL, *offset - 1);
	}

	return r;
}

void prefix_recursive_level_out(const int recursive_level)
{
	/* Yes, we could check whether or not opt_recursive_pattern is empty.
	   We could.
	*/
	int i;
	for (i = 0; i < recursive_level; ++i) {
		out(opt_recursive_pattern);
	}
}

void out_sequence(size_t offset, char *buf, const unsigned long len, const int is_value, const out_data_t od, const int recursive_level)
{

	const unsigned long hbytes = opt_width / 2;

	char *str = malloc(opt_width + 50);
	int strpos = 0;

	if (!is_value || od == OD_ENFORCE_HEX) {
		unsigned long i;
		unsigned long lim = ((len + opt_width - 1) / opt_width) * opt_width;
		if (!lim)
			lim = (unsigned long)opt_width;
		for (i = 0; i < lim; ++i) {
			if (!(i % opt_width)) {
				prefix_recursive_level_out(recursive_level);
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
		prefix_recursive_level_out(recursive_level);
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
	if (tag_class == TAG_CLASS_UNIVERSAL && (size_t)tag_number < sizeof(universal_class_tags) / sizeof(*universal_class_tags)) {
		s_strncpy(s, universal_class_tags[tag_number].name, slen);
	} else if (tag_class == TAG_CLASS_UNIVERSAL) {
		snprintf(s, slen, "![ %i ]", tag_number);
	} else {
		snprintf(s, slen, "[ %i ]", tag_number);
	}
}

int parse_taglength(FILE **F, size_t *offset, ssize_t *remaining_length, taglength_t *tl, const int recursive_level,
					int loose_read)
{
	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(F, offset, loose_read)) == EOF)
		return 0;

	tl->hbytes[0] = c;
	tl->hlen = 0;
	tl->class = (c & 0xc0) >> 6;
	tl->p_or_c = (c & 0x20) >> 5;
	int original_type = tl->p_or_c;
	tl->number = (c & 0x1F);
	int type = universal_class_tags[tl->number].type;
	if (tl->class == TAG_CLASS_UNIVERSAL && type >= 0 && type <= 1 && tl->p_or_c != type) {
		tl->p_or_c = TAG_TYPE_PRIMITIVE;
		out_err("Warning: primitive/constructed bit mismatch, enforcing primitive\n");
	}
	char tag_name[100];
	get_tag_name(tag_name, sizeof(tag_name), tl->class, tl->number);

/*    char buf[TAG_U_LONG_FORMAT_MAX_BYTES + LENGTH_MULTIBYTES_MAX_BYTES];*/

/*    buf[0] = (char)c;*/
	if (opt_ol >= OL_VERBOSE)
		out_sequence(old_offset, tl->hbytes, 1, 0, OD_UNDEF, recursive_level);

	if (opt_ol == OL_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)c);
		out("%c%c %c %c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		out("^^         Tag class:  %3i -> %s\n", tl->class, classes[tl->class]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		out("   ^       Tag type:   %3i -> %s\n", original_type, PCs[original_type]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		out("     ^^^^^ Tag number: %3i -> %s\n", tl->number, tag_name);
	} else if (opt_ol >= OL_VERBOSE) {
		out("%s-%s: %s\n", short_classes[tl->class], short_PCs[original_type], tag_name);
	}

	int pos = 0;

	if (tl->number == TAG_U_LONG_FORMAT) {
		pos = 1;

		old_offset = *offset;
		do {
			if ((c = myfgetc(F, offset, FALSE)) == EOF)
				return 0;
			tl->hbytes[pos] = c;
			--(*remaining_length);
			++(tl->hlen);
		} while (tl->hbytes[pos] & 0x80 && ++pos < TAG_U_LONG_FORMAT_MAX_BYTES);
		if (pos == sizeof(tl->hbytes)) {
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
				bm1 = (unsigned)tl->hbytes[rev - 1];
			rmask = (0x7Fu >> shift);
			lmask = (0xFFu << (7 - shift)) & 0xFFu;
			v0 = (long unsigned)(((bm1 << (7 - shift)) & lmask) | (((unsigned)tl->hbytes[rev] >> shift) & rmask));

			value += v0 * multi;
			multi *= 256;   /* Can be written <<8, but... */
			++shift;
		}
		tl->number = (int)value;
		if (opt_ol >= OL_VERBOSE) {
			out_sequence(old_offset, tl->hbytes + 1, (unsigned long)pos, 0, OD_UNDEF, recursive_level);
			out("Tag number: %i\n", tl->number);
		}
	}

	get_tag_name(tag_name, sizeof(tag_name), tl->class, tl->number);

	if (tl->class == TAG_CLASS_UNIVERSAL && tl->number >= 31) {
		out_err("Warning: universal tag number above maximum allowed value (30)\n");
	}

	--(*remaining_length);
	++(tl->hlen);

	if (!check_position(F, *offset, *remaining_length, 0))
		return 0;

	char *bl = tl->hbytes + pos + 1;

	int cc;
	size_t old_loffset = *offset;
	if ((cc = myfgetc(F, offset, FALSE)) == EOF)
		return 0;
	int n = 0;
	bl[0] = (char)cc;
	tl->length = (unsigned long)cc;
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
		tl->length = 0;
		int i;
		for (i = 1; i <= n; ++i) {
			if ((cc = myfgetc(F, offset, FALSE)) == EOF)
				return 0;
			tl->length <<= 8;
			tl->length += (unsigned int)cc;
			bl[i] = (char)cc;
		}
	}
	if (opt_ol >= OL_VERBOSE)
		out_sequence(old_loffset, bl, (unsigned long)n + 1, 0, OD_UNDEF, recursive_level);

	if (opt_ol == OL_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)(bl[0]));
		out("%c %c%c%c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		out("^          Len type: %i -> %s\n", length_type, length_coding_types[length_type]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		if (length_type == LENGTH_CODE_TYPE_ONE) {
			out("  ^^^^^^^  Len: %lu", tl->length);
		} else {
			out("  ^^^^^^^  Len nb bytes: %i, value: %lu", n, tl->length);
		}
		out("\n");
	} else if (opt_ol >= OL_VERBOSE) {
		out("Length: %lu\n", tl->length);
	}

	if (opt_ol == OL_NORMAL) {
		out_sequence(old_offset, tl->hbytes, (size_t)(pos + n + 2), 0, OD_UNDEF, recursive_level);
		out("%s-%s: %s, len: %lu\n", short_classes[tl->class], short_PCs[original_type], tag_name, tl->length);
	}

	*remaining_length -= (n + 1);
	tl->hlen += (n + 1);

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

void parse(FILE **F, size_t *offset, ssize_t *remaining_length, const int recursive_level, int loose_read)
{
	taglength_t tl;

	if (recursive_level > MAX_RECURSIVE_LEVELS) {
		myfclose(F, "number of recursive calls limit reached, now stopping", *offset);
	}

	if (!parse_taglength(F, offset, remaining_length, &tl, recursive_level, loose_read))
		return;

	if (tl.p_or_c == TAG_TYPE_CONSTRUCTED) {
		ssize_t inner_remaining_length = (ssize_t)tl.length;
		while(inner_remaining_length >= 1 && *F != NULL) {
			parse(F, offset, &inner_remaining_length, recursive_level + 1, FALSE);
		}
		if (inner_remaining_length && *F != NULL) {
			myfclose(F, "contained data out of container boundaries", *offset);
			return;
		}
		*remaining_length -= (ssize_t)tl.length;
	} else {
		char *buf;
		buf = (char *)malloc(tl.length);
		size_t nbread;
		nbread = fread(buf, 1, (size_t)tl.length, *F);
		size_t old_offset = *offset;
		*offset += nbread;
		*remaining_length -= (ssize_t)nbread;

		if (nbread >= 1) {
			out_data_t od = opt_od;
			if (opt_od == OD_SMART && tl.class == TAG_CLASS_UNIVERSAL &&
				(size_t)tl.number < sizeof(universal_class_tags) / sizeof(*universal_class_tags)) {
				od = universal_class_tags[tl.number].is_string ? OD_ENFORCE_TEXT : OD_ENFORCE_HEX;
			}
			out_sequence(old_offset, buf, nbread, 1, od, recursive_level);
		}

		if (nbread != tl.length) {
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
			if (tl.class == TAG_CLASS_UNIVERSAL && tl.number == TAG_U_OBJECT_IDENTIFIER) {
				const size_t sizeof_printable = 200;
				char *printable = malloc(sizeof_printable);

#ifdef HAS_LIB_OPENSSL
				unsigned char *tmp = malloc(tl.hlen + tl.length);
				memcpy(tmp, tl.hbytes, tl.hlen);
				memcpy(tmp + tl.hlen, buf, tl.length);
				ASN1_OBJECT *oid = NULL;
				const unsigned char *tt = tmp;
				const char *sn = "";
				if (d2i_ASN1_OBJECT(&oid, &tt, tl.hlen + tl.length) != NULL) {
					const char *soid;
					soid = OBJ_nid2sn(OBJ_obj2nid(oid));
					sn = (const char *)soid;
					ASN1_OBJECT_free(oid);
				}
				free(tmp);
#endif

				if (!decode_oid(printable, sizeof_printable, buf, tl.length)) {
					out("\n");
					myfclose(F, "unable to decode OID", *offset);
				} else {
#ifdef HAS_LIB_OPENSSL
					out("OID: %s (%s)", printable, sn);
#else
					out("OID: %s", printable);
#endif
				}
				free(printable);
			}
		}
		if (*F == NULL) {
			free(buf);
			return;
		}
		free(buf);
		if (nbread >= 1)
			out("\n");
	}
	return;
}

void opt_check(int n, const char *opt)
{
	static int defined_options[6] = {0, 0, 0};

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
	int optset_hex = 0;
	s_strncpy(opt_recursive_pattern, "", sizeof(opt_recursive_pattern));

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
			opt_od = OD_ENFORCE_HEX;
		} else if (!strcasecmp(argv[a], "-text")) {
			opt_check(2, argv[a]);
			optset_text = 1;
			opt_od = OD_ENFORCE_TEXT;
		} else if (!strcasecmp(argv[a], "-hex")) {
			opt_check(3, argv[a]);
			optset_hex = 1;
			opt_od = OD_ENFORCE_HEX;
		} else if (!strcasecmp(argv[a], "-recursive")) {
			opt_check(4, argv[a]);
			if (++a >= argc) {
				a = -(a - 1);
			} else {
				s_strncpy(opt_recursive_pattern, argv[a], sizeof(opt_recursive_pattern));
			}
		} else if (!strcasecmp(argv[a], "-width")) {
			opt_check(5, argv[a]);
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
	} else if (optset_hex && optset_text) {
		out_err("incompatible options -text and -hex\n");
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
	do {
		parse(&F, &offset, &s, 0, TRUE);
	} while (F != NULL);
	if (F != NULL && F != stdin) {
		if (s != 0 || fgetc(F) != EOF) {
			out_dbg("s = %li\n", s);
			myfclose(&F, "Trailing characters in file", offset);
			return -5;
		} else {
			myfclose(&F, NULL, 0);
		}
	} else {
		return -99;
	}
	return 0;
}

