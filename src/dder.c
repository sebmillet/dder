/* dder.c */

/* Parse der files */

/* Sébastien Millet, 2015 - 2016 */

/*#define DEBUG*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "..\extracfg.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <termios.h>
#endif

#ifdef HAS_LIB_OPENSSL
#include <openssl/objects.h>
#endif

#include "ppem.h"

#if defined(_WIN32) || defined(_WIN64)
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

	/* Arbitrary limit. Just to avoid crazy values that'd eat up all available memory. */
#define MAX_DATA_BLOCK_LEN 104857600

	/* Includes the initial byte of tag number 31 */
#define TAG_U_LONG_FORMAT_MAX_BYTES 6
	/* Includes the initial byte that indicates the number of bytes used to encode length */
#define LENGTH_MULTIBYTES_MAX_BYTES 7

typedef struct {
	int class;
	int p_or_c;
	int number;
	unsigned long length;
	int indefinite;
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
	{"EOC", 0, 0},               /* TAG_U_EOC */
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
#define TAG_U_EOC               0
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
typedef enum {L_ENFORCE = 0, L_NORMAL = 1, L_VERBOSE = 2, L_VERYVERBOSE = 3} out_level_t;
out_level_t opt_ol = L_NORMAL;

	/* OD stands for Out Data */
typedef enum {OD_UNDEF, OD_SMART, OD_ENFORCE_HEX, OD_ENFORCE_TEXT} out_data_t;
out_data_t opt_od = OD_SMART;

	/* Nb bytes output on each line. MUST be an even number */
long unsigned opt_width = 16;

	/* Prefix to add as many times as current recursive level */
char opt_recursive_pattern[100];

int opt_der = FALSE;

const char *opt_password = NULL;


/*

========================================================================

*/

#define UNUSED(x) (void)(x)

#define assert(b) \
if (!(b)) { \
	out_err("FATAL ERROR: file %s, line %i\n", __FILE__, __LINE__); \
	exit(-999); \
}

int out(out_level_t l, const char *fmt, ...)
{
	if (l > opt_ol)
		return 0;

	va_list args;
	va_start(args, fmt);
	int r = vprintf(fmt, args);
	va_end(args);
	return r;
}

int outln(out_level_t l, const char *fmt, ...)
{
	if (l > opt_ol)
		return 0;

	va_list args;
	va_start(args, fmt);
	int r = vprintf(fmt, args);
	r = printf("\n");
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

enum {VF_UNINITIALIZED, VF_FILE, VF_MEM};

typedef struct vf_t vf_t;
struct vf_t {
	int type;

/* type = VF_FILE */

	FILE *FILE_f;

/* type = VF_MEM */

	const unsigned char *MEM_data;
	size_t MEM_data_len;
	size_t MEM_idx;
};

vf_t *vf_construct()
{
	vf_t *vf = (vf_t *)malloc(sizeof(vf_t));
	vf->type = VF_UNINITIALIZED;
	return vf;
}

void vf_destruct(vf_t *vf)
{
	free(vf);
}

int vf_is_initialized(vf_t *vf)
{
	return (vf->type != VF_UNINITIALIZED);
}

void vf_init_FILE(vf_t *vf, FILE *f)
{
	assert(vf && !vf_is_initialized(vf));
	vf->type = VF_FILE;
	vf->FILE_f = f;
}

void vf_init_MEM(vf_t *vf, const unsigned char *data, size_t data_len)
{
	assert(vf && !vf_is_initialized(vf));
	vf->type = VF_MEM;
	vf->MEM_data = data;
	vf->MEM_data_len = data_len;
	vf->MEM_idx = 0;
}

int vf_fgetc(vf_t *vf)
{
	if (!vf_is_initialized(vf))
		return EOF;

	if (vf->type == VF_FILE) {
		return fgetc(vf->FILE_f);
	} else if (vf->type == VF_MEM) {
		if (vf->MEM_idx >= vf->MEM_data_len) {
			if (vf->MEM_idx == vf->MEM_data_len)
				vf->MEM_idx++;
			return EOF;
		} else {
			return vf->MEM_data[vf->MEM_idx++];
		}
	} else
		assert(FALSE);
}

size_t vf_fread(void *ptr, size_t size, size_t nmemb, vf_t *vf)
{
	if (!vf_is_initialized(vf))
		return 0;

	if (vf->type == VF_FILE) {
		return fread(ptr, size, nmemb, vf->FILE_f);
	} else if (vf->type == VF_MEM) {
		if (vf->MEM_idx >= vf->MEM_data_len)
			return 0;

			/* We are certain that 'remaining' is '>= 1' */
		size_t remaining = vf->MEM_data_len - vf->MEM_idx;

		size_t nb_bytes = size * nmemb;
		int flag1 = 0;
		if (nb_bytes > remaining) {
			flag1 = 1;
			nb_bytes = remaining;
		}

		memcpy(ptr, &vf->MEM_data[vf->MEM_idx], nb_bytes);
		vf->MEM_idx += nb_bytes + flag1;
		return nb_bytes;
	} else
		assert(FALSE);
}

int vf_feof(vf_t *vf)
{
	if (!vf_is_initialized(vf))
		return TRUE;

	if (vf->type == VF_FILE) {
		return feof(vf->FILE_f);
	} else if (vf->type == VF_MEM) {
		return (vf->MEM_idx > vf->MEM_data_len);
	} else
		assert(FALSE);
}

void usage()
{
	fprintf(stderr, "Usage:\n"
		"  dder [OPTION]... [FILE]\n"
		"Parses FILE according to DER syntax\n"
		"Uses standard input if FILE is not specified\n"
		"\n"
		"dder will automatically detect whether input data is\n"
		"DER encoded or PEM encoded, unless you specify -der option.\n"
		"\n"
		"Options are case insensitive. Options list:\n"
		"  -help        print this help screen\n"
		"  -version     output version information and exit\n"
		"  -verbose     verbose output.\n"
		"  -veryverbose *very* verbose output. Implies -hex\n"
		"  -text        enforce text output of data values\n"
		"  -hex         enforce hex output of data values\n"
		"  -width N     number of bytes per line, must be even\n"
		"  -password p  use password 'p' to decrypt data (PEM encoding)\n"
		"  -der         input data encoding is DER\n"
		"  -recursive S string to add at the beginning of output lines,\n"
		"               one occurence of string for each level\n"
		"  --           end of parameters, next option is a file name\n"
	);
	exit(-1);
}

void version()
{
	out(L_ENFORCE, PACKAGE_NAME " " VERSION);
#ifdef DEBUG
	out(L_ENFORCE, "d");
#endif
	outln(L_ENFORCE, "");
	outln(L_ENFORCE, "Copyright 2015, 2016 Sébastien Millet.");
	outln(L_ENFORCE, "This is free software with ABSOLUTELY NO WARRANTY.");
	exit(0);
}

char *s_strncpy(char *dest, const char *src, size_t n)
{
		strncpy(dest, src, n);
		dest[n - 1] = '\0';
		return dest;
}
	/* The define below triggers an error if usual strncpy is used */
#define strncpy(a, b, c) ErrorDontUse_strncpy_Use_s_strncpy_Instead

char *s_strncat(char *dest, const char *src, size_t dest_len)
{
	size_t l = strlen(dest);
	assert(l < dest_len);

	size_t n = dest_len - l - 1;
	if (n >= 1)
		strncat(dest, src, n);

		/* strncat manual says dest will always get a null byte at the end
		 * but I want something robust across systems and time... */
	dest[dest_len - 1] = '\0';

	return dest;
}
	/* The define below triggers an error if usual strncat is used */
#define strncat(a, b, c) ErrorDontUse_strncat_Use_s_strncat_Instead

	/*
	 * Returns a copied, allocated string. Uses s_strncpy for the string
	 * copy (see comment above).
	 * dst can be null, in which case the new string is to be retrieved
	 * by the function return value.
	 */
char *s_alloc_and_copy(char **dst, const char *src)
{
	unsigned int s = strlen(src) + 1;
	char *target = (char *)malloc(s);
	s_strncpy(target, src, s);
	if (dst)
		*dst = target;
	return target;
}

ssize_t file_get_size(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

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

void myfclose(vf_t *vf, const char *err, size_t position)
{
	if (err != NULL)
		out_err("Error: %s, position: %u\n", err, (unsigned int)position);
	if (vf->type == VF_FILE) {
		if (vf->FILE_f != stdin)
			fclose(vf->FILE_f);
		vf->FILE_f = NULL;
	}
	vf->type = VF_UNINITIALIZED;
}

int myfgetc(vf_t *vf, size_t *offset, int loose_read)
{
	if (!vf_is_initialized(vf))
		return EOF;

	++(*offset);
	int r = vf_fgetc(vf);
	if (r == EOF) {
		if (!loose_read)
			myfclose(vf, "unexpected end of file", *offset - 1);
		else
			myfclose(vf, NULL, *offset - 1);
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
		out(L_ENFORCE, opt_recursive_pattern);
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
					out(L_ENFORCE, "%06x  ", (unsigned int)offset);
				else
					out(L_ENFORCE, "        ");
			}
			if (!((i + hbytes) % opt_width)) {
				out(L_ENFORCE, "   ");
				str[strpos++] = ' ';
				str[strpos++] = ' ';
			}
			if (i < len) {
				out(L_ENFORCE, "%02x ", (unsigned char)buf[i]);
				str[strpos++] = (buf[i] < ' ' || buf[i] == 127 ? '.' : buf[i]);
			} else {
				out(L_ENFORCE, "   ");
				str[strpos++] = ' ';
			}
			if (!((i + 1) % opt_width)) {
				out(L_ENFORCE, "  ");
				if (is_value) {
					str[strpos] = '\0';
					out(L_ENFORCE, "%s ", str);
				}
				if (i + 1 < lim) {
					outln(L_ENFORCE, "");
					offset += opt_width;
				}
			}
		}
	} else {
		prefix_recursive_level_out(recursive_level);
		out(L_ENFORCE, "%06x  ", (unsigned int)offset);
		char c;
		unsigned i;
		for (i = 0; i < len; ++i) {
			c = buf[i];
			if (c < ' ' || c == 127)
				c = '.';
			out(L_ENFORCE, "%c", c);
		}
		out(L_ENFORCE, "  ");
	}
	free(str);
}

int check_position(vf_t *vf, const size_t offset, const ssize_t remaining_length, const int insert_newline)
{
	if (!vf_is_initialized(vf))
		return 0;
	if (remaining_length < 0) {
		if (insert_newline)
			outln(L_ENFORCE, "");
		myfclose(vf, "inconsistent items length", offset);
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

int parse_taglength(vf_t *vf, size_t *offset, ssize_t *remaining_length, taglength_t *tl, const int recursive_level,
					int loose_read, ssize_t *consumed)
{
	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(vf, offset, loose_read)) == EOF)
		return 0;

	tl->hbytes[0] = (char)c;
	tl->hlen = 0;
	tl->class = (c & 0xc0) >> 6;
	tl->p_or_c = (c & 0x20) >> 5;
	tl->number = (c & 0x1F);
	tl->indefinite = FALSE;
	int type = universal_class_tags[tl->number].type;
	if (tl->class == TAG_CLASS_UNIVERSAL && type >= 0 && type <= 1 && tl->p_or_c != type) {
		tl->p_or_c = TAG_TYPE_PRIMITIVE;
		out_err("Warning: primitive/constructed bit mismatch, enforcing primitive\n");
	}
	char tag_name[100];
	get_tag_name(tag_name, sizeof(tag_name), tl->class, tl->number);

	if (opt_ol >= L_VERBOSE)
		out_sequence(old_offset, tl->hbytes, 1, 0, OD_UNDEF, recursive_level);

	if (opt_ol == L_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)c);
		outln(L_ENFORCE, "%c%c %c %c%c%c%c%c", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		outln(L_ENFORCE, "^^         Tag class:  %3i -> %s", tl->class, classes[tl->class]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		outln(L_ENFORCE, "   ^       Tag type:   %3i -> %s", tl->p_or_c, PCs[tl->p_or_c]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		outln(L_ENFORCE, "     ^^^^^ Tag number: %3i -> %s", tl->number, tag_name);
	} else if (opt_ol >= L_VERBOSE) {
		outln(L_ENFORCE, "%s-%s: %s", short_classes[tl->class], short_PCs[tl->p_or_c], tag_name);
	}

	int pos = 0;

	if (tl->number == TAG_U_LONG_FORMAT) {
		pos = 1;

		old_offset = *offset;
		do {
			if ((c = myfgetc(vf, offset, FALSE)) == EOF)
				return 0;
			tl->hbytes[pos] = (char)c;
			--(*remaining_length);
			++(tl->hlen);
		} while (tl->hbytes[pos] & 0x80 && ++pos < TAG_U_LONG_FORMAT_MAX_BYTES);
		if (pos == sizeof(tl->hbytes)) {
			myfclose(vf, "Tag number too big", old_offset);
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
		if (opt_ol >= L_VERBOSE) {
			out_sequence(old_offset, tl->hbytes + 1, (unsigned long)pos, 0, OD_UNDEF, recursive_level);
			outln(L_ENFORCE, "Tag number: %i", tl->number);
		}
	}

	get_tag_name(tag_name, sizeof(tag_name), tl->class, tl->number);

	if (tl->class == TAG_CLASS_UNIVERSAL && tl->number >= 31) {
		out_err("Warning: universal tag number above maximum allowed value (30)\n");
	}

	--(*remaining_length);
	++(tl->hlen);

	if (!check_position(vf, *offset, *remaining_length, 0))
		return 0;

	char *bl = tl->hbytes + pos + 1;

	int cc;
	size_t old_loffset = *offset;
	if ((cc = myfgetc(vf, offset, FALSE)) == EOF)
		return 0;
	int n = 0;
	bl[0] = (char)cc;
	tl->length = (unsigned long)cc;
	int length_type = ((cc & 0x80) >> 7);
	if (cc & 0x80) {
		n = (cc & 0x7F);
		if (n > LENGTH_MULTIBYTES_MAX_BYTES - 1) {
			myfclose(vf, "number of bytes to encode length exceeds maximum", old_loffset);
			return 0;
		} else if (n == 0 && tl->p_or_c != TAG_TYPE_CONSTRUCTED) {
			myfclose(vf, "number of bytes to encode length cannot be null for a primitive", old_loffset);
			return 0;
		}
		tl->length = 0;
		int i;
		for (i = 1; i <= n; ++i) {
			if ((cc = myfgetc(vf, offset, FALSE)) == EOF)
				return 0;
			tl->length <<= 8;
			tl->length += (unsigned int)cc;
			bl[i] = (char)cc;
		}
		if (!tl->length)
			tl->indefinite = TRUE;
	}
	if (opt_ol >= L_VERBOSE)
		out_sequence(old_loffset, bl, (unsigned long)n + 1, 0, OD_UNDEF, recursive_level);

	if (opt_ol == L_VERYVERBOSE) {
		char b[9];
		char_8bit_to_bin_str(b, sizeof(b), (unsigned char)(bl[0]));
		outln(L_ENFORCE, "%c %c%c%c%c%c%c%c", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		outln(L_ENFORCE, "^          Len type: %i -> %s", length_type, length_coding_types[length_type]);
		out_sequence(0, NULL, 0, 0, OD_UNDEF, recursive_level);
		if (length_type == LENGTH_CODE_TYPE_ONE) {
			out(L_ENFORCE, "  ^^^^^^^  Len: %lu", tl->length);
		} else {
			out(L_ENFORCE, "  ^^^^^^^  Len nb bytes: %i, value: %lu", n, tl->length);
		}
		outln(L_ENFORCE, "");
	} else if (opt_ol >= L_VERBOSE) {
		outln(L_ENFORCE, "Length: %lu", tl->length);
	}

	if (opt_ol == L_NORMAL) {
		out_sequence(old_offset, tl->hbytes, (size_t)(pos + n + 2), 0, OD_UNDEF, recursive_level);
		outln(L_ENFORCE, "%s-%s: %s, len: %lu", short_classes[tl->class], short_PCs[tl->p_or_c], tag_name, tl->length);
	}

	*remaining_length -= (n + 1);
	tl->hlen += (n + 1);
	*consumed += tl->hlen;

		/* FIXME */
/*    printf("*consumed = %lu, tl->hlen = %i\n", *consumed, tl->hlen);*/

	return check_position(vf, *offset, *remaining_length, 0);
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

int parse(vf_t *vf, size_t *offset, ssize_t *remaining_length, const int recursive_level, int loose_read,
			ssize_t *consumed, int is_inside_indefinite)
{
	taglength_t tl;

	if (recursive_level > MAX_RECURSIVE_LEVELS) {
		myfclose(vf, "number of recursive calls limit reached, now stopping", *offset);
	}

	if (!parse_taglength(vf, offset, remaining_length, &tl, recursive_level, loose_read, consumed))
		return FALSE;
	if (tl.length > MAX_DATA_BLOCK_LEN) {
		myfclose(vf, "data length too big", *offset);
		return FALSE;
	}

	int is_EOC = FALSE;

	if (tl.p_or_c == TAG_TYPE_CONSTRUCTED) {
		ssize_t inner_remaining_length = (ssize_t)tl.length;
		if (tl.indefinite) {
			assert(!tl.length);
			inner_remaining_length = INT_MAX;
		}
		ssize_t inner_consumed = 0;
		while(inner_remaining_length >= 1 && vf_is_initialized(vf)) {

				/* FIXME */
/*            printf("Offset: %04X, inner_consumed: %04X\n", *offset, inner_consumed);*/

			if (parse(vf, offset, &inner_remaining_length, recursive_level + 1, FALSE, &inner_consumed, tl.indefinite)) {
				if (tl.indefinite)
					break;
			}
		}
		if (!tl.indefinite && inner_remaining_length && vf_is_initialized(vf)) {
			myfclose(vf, "contained data out of container boundaries", *offset);
			return FALSE;
		}
/*        *remaining_length -= (ssize_t)tl.length;*/
		*remaining_length -= inner_consumed;
		*consumed += inner_consumed;
	} else {
		char *buf = NULL;

		size_t nbread = 0;
		if (tl.length) {
			buf = (char *)malloc(tl.length);
			nbread = vf_fread(buf, 1, (size_t)tl.length, vf);
			size_t old_offset = *offset;
			*offset += nbread;
			*remaining_length -= (ssize_t)nbread;
			*consumed += (ssize_t)nbread;

			if (nbread >= 1) {
				out_data_t od = opt_od;
				if (opt_od == OD_SMART && tl.class == TAG_CLASS_UNIVERSAL &&
					(size_t)tl.number < sizeof(universal_class_tags) / sizeof(*universal_class_tags)) {
					od = universal_class_tags[tl.number].is_string ? OD_ENFORCE_TEXT : OD_ENFORCE_HEX;
				}
				out_sequence(old_offset, buf, nbread, 1, od, recursive_level);
			}
		}

		if (nbread != tl.length) {
			if (vf_feof(vf)) {
				outln(L_ENFORCE, "");
				myfclose(vf, "unexpected end of file", *offset);
			} else {
				out_errno((ssize_t)*offset);
				myfclose(vf, NULL, 0);
			}
		} else {
			if (!check_position(vf, *offset, *remaining_length, 1)) {
				free(buf);
				return FALSE;
			}
			if (tl.class == TAG_CLASS_UNIVERSAL && tl.number == TAG_U_EOC) {
				is_EOC = TRUE;
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
					outln(L_ENFORCE, "");
					myfclose(vf, "unable to decode OID", *offset);
				} else {
#ifdef HAS_LIB_OPENSSL
					out(L_ENFORCE, "OID: %s (%s)", printable, sn);
#else
					out(L_ENFORCE, "OID: %s", printable);
#endif
				}
				free(printable);
			}
		}
		if (!vf_is_initialized(vf)) {
			free(buf);
			return is_EOC;
		}
		free(buf);
		if (nbread >= 1)
			outln(L_ENFORCE, "");
	}
	return is_EOC;
}

#ifdef HAS_LIB_OPENSSL

char *cb_password_pre()
{
/* FIXME */
#define PASSWORD_MAX_BYTES 200

	char *password;

	if (!opt_password) {

#if defined(_WIN32) || defined(_WIN64)
		HANDLE h;
		DWORD console_mode;
		h = GetStdHandle(STD_INPUT_HANDLE);
		if (!GetConsoleMode(h, &console_mode))
			return NULL;
		if (!SetConsoleMode(h, console_mode & ~ENABLE_ECHO_INPUT))
			return NULL;
#else
		struct termios current, new;
		if (tcgetattr(fileno(stdin), &current) != 0)
			return NULL;
		new = current;
		new.c_lflag &= ~ECHO;
		if (tcsetattr(fileno(stdin), TCSAFLUSH, &new) != 0)
			return NULL;
#endif

		printf("Please type in the password:\n");
		char *readpwd = malloc(PASSWORD_MAX_BYTES);
		char *r = fgets(readpwd, PASSWORD_MAX_BYTES, stdin);

#if defined(_WIN32) || defined(_WIN64)
		SetConsoleMode(h, console_mode);
#else
		tcsetattr(fileno(stdin), TCSAFLUSH, &current);
#endif

		if (!r) {
			free(readpwd);
			return NULL;
		}
		readpwd[PASSWORD_MAX_BYTES - 1] = '\0';
		password = readpwd;
	} else {
		password = s_alloc_and_copy(NULL, opt_password);
	}

	int i;
	for (i = 0; i < 2; ++i) {
		int n = strlen(password);
		if (n >= 1 && (password[n - 1] == '\n' || password[n - 1] == '\r'))
			password[n - 1] = '\0';
	}

/*    DBG("Password: '%s'", password)*/

	return password;
}

void cb_password_post(char *password)
{
	if (password)
		free(password);
}

void print_hexa(out_level_t l, const unsigned char *buf, int buf_len) {
	int i; for (i = 0; i < buf_len; ++i) out(l, "%02X", (unsigned char)buf[i]);
}

void cb_loop_top(const pem_ctrl_t *ctrl)
{
	if (!pem_has_data(ctrl)) {
		outln(L_VERBOSE, "PEM block: [%s] (skipped: %s)", pem_header(ctrl), pem_errorstring(pem_status(ctrl)));
		return;
	}

	if (pem_has_encrypted_data(ctrl)) {
		out(L_VERBOSE, "PEM block: [%s] (encrypted with %s", pem_header(ctrl), pem_cipher(ctrl));
		if (!pem_salt(ctrl)) {
			outln(L_VERBOSE, ", no salt)");
		} else {
			out(L_VERBOSE, ", salt: ");
			print_hexa(L_VERBOSE, pem_salt(ctrl), pem_salt_len(ctrl));
			outln(L_VERBOSE, ")");
		}
	} else {
		outln(L_VERBOSE, "PEM block: [%s]", pem_header(ctrl));
	}
}

void cb_loop_decrypt(int decrypt_ok, const char *errmsg)
{
	if (!decrypt_ok)
		out_err("%s\n", errmsg);
}

#endif /* #ifdef HAS_LIB_OPENSSL */
void opt_check(int n, const char *opt)
{
	static int defined_options[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	if (defined_options[n]) {
		out_err("Option %s already set\n", opt);
		exit(-2);
	} else
		defined_options[n] = 1;
}

int main(int argc, char **argv)
{
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
			opt_ol = L_VERBOSE;
		} else if (!strcasecmp(argv[a], "-veryverbose")) {
			opt_check(1, argv[a]);
			optset_veryverbose = 1;
			opt_ol = L_VERYVERBOSE;
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
		} else if (!strcasecmp(argv[a], "-der")) {
			opt_check(6, argv[a]);
			opt_der = TRUE;
		} else if (!strcasecmp(argv[a], "-password")) {
			opt_check(7, argv[a]);
			if (++a >= argc) {
				a = -(a - 1);
			} else {
				opt_password = argv[a];
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

	char *fname = NULL;
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
	} else if (a >= 1 && a < argc) {
		fname = argv[a];
	}
	if (a <= 0)
		usage();

	if (!fname)
		outln(L_ENFORCE, "Reading from standard input...");
	else
		outln(L_ENFORCE, "Reading from file %s...", fname);

	vf_t *vf = NULL;
	int return_code = 1;
	unsigned char *data_in = NULL;
	ssize_t data_in_size = 0;
	unsigned char *data_out = NULL;
	size_t data_out_len = 0;
	int data_in_is_pem = FALSE;

#ifdef HAS_LIB_OPENSSL
const size_t STDIN_BUFSIZE = 1024;

	if (!opt_der) {
		if (!fname) {
			while (!feof(stdin)) {
				size_t next_size = data_in_size + STDIN_BUFSIZE;
				data_in = realloc(data_in, next_size);
				size_t nr = fread(&data_in[data_in_size], 1, STDIN_BUFSIZE, stdin);
				if (nr != STDIN_BUFSIZE) {
					if (ferror(stdin) || !feof(stdin)) {
						out_err("reading input\n");
						goto main_terminate;
					}
				}
				data_in_size += nr;
			}
			data_in = realloc(data_in, data_in_size);
		} else {
			FILE *fin;
			if ((data_in_size = file_get_size(fname)) < 0) {
				out_errno(-1);
				goto main_terminate;
			}
			if (!(fin = fopen(fname, "rb"))) {
				out_errno(-1);
				goto main_terminate;
			}
			data_in = malloc(data_in_size + 1);
			if ((ssize_t)fread(data_in, 1, data_in_size, fin) != data_in_size) {
				out_errno(-1);
				goto main_terminate;
			}
			fclose(fin);
		}

		if (data_in) {
				/* *VERY IMPORTANT* */
				/* WARNING
				 * This character is used to mark end of buffer in the case the input
				 * is PEM format. */
			data_in[data_in_size] = '\0';
		}

		outln(L_VERBOSE, "Trying to parse input data against PEM rules");
		pem_ctrl_t *pem = pem_construct_pem_ctrl(data_in);
		pem_regcb_password(pem, cb_password_pre, cb_password_post);
		pem_regcb_loop_top(pem, cb_loop_top);
		pem_regcb_loop_decrypt(pem, cb_loop_decrypt);
		data_in_is_pem = pem_walker(pem, &data_out, &data_out_len);
		pem_destruct_pem_ctrl(pem);

	}
	int assume_der = opt_der;
#else  /* !HAS_LIB_OPENSSL */
	int assume_der = TRUE;
#endif /* HAS_LIB_OPENSSL */

	vf = vf_construct();

	const unsigned char *pkdata = NULL;
	size_t pkdata_len = 0;

	ssize_t s;

	if (assume_der) {
		FILE *ff;
		if (fname) {
			if ((s = file_get_size(fname)) < 0) {
				out_errno(-1);
				goto main_terminate;
			}
			if (!(ff = fopen(fname, "rb"))) {
				out_errno(-1);
				goto main_terminate;
			}
		} else {
			ff = stdin;
			s = INT_MAX;
		}
		vf_init_FILE(vf, ff);
	} else if (!data_in_is_pem) {
		outln(L_VERBOSE, "Will use original data as pk input (assuming der-encoded content)");
		pkdata = data_in;
		pkdata_len = data_in_size;
		s = pkdata_len;
	} else {
		outln(L_VERBOSE, "Will use pem decoded/decrypted data as pk input");
		pkdata = data_out;
		pkdata_len = data_out_len;
		if (!pkdata || data_out_len == 0) {
			out_err("No PEM data available\n");
			goto main_terminate;
		}
		s = pkdata_len;
	}
	if (!assume_der && pkdata)
		vf_init_MEM(vf, pkdata, pkdata_len);

	size_t offset = 0;
	do {
		ssize_t consumed = 0;
		parse(vf, &offset, &s, 0, TRUE, &consumed, FALSE);
	} while (vf_is_initialized(vf));
	if (vf_is_initialized(vf) && fname) {
		if (s != 0 || vf_fgetc(vf) != EOF) {
			out_dbg("s = %li\n", s);
			myfclose(vf, "Trailing characters in file", offset);
			goto main_terminate;
		} else {
			myfclose(vf, NULL, 0);
		}
	} else {
		goto main_terminate;
	}

	return_code = 0;

main_terminate:

	if (vf)
		vf_destruct(vf);
	if (data_in)
		free(data_in);
	if (data_out)
		free(data_out);
	return return_code;
}

