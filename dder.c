/* dder.c */

/* Parse der files */

/* Sébastien Millet, February 2015 */

/*#define DEBUG*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

int snprintf(char *str, size_t size, const char *format, ...);

#define UNUSED(x) (void)(x)

int opt_verbose = 0;

#define out(args...) \
	printf(args)

#ifdef DEBUG
#define out_dbg(args...) \
	out(args)
#else
#define out_dbg(args...) ;
#endif

void out_errno(const ssize_t offset)
{
	if (offset >= 0)
		out("Error: %s, position %u.\n", strerror(errno), (unsigned int)offset);
	else
		out("Error: %s.\n", strerror(errno));
}

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
#define CLASS_UNIVERSAL        0
#define CLASS_APPLICATION      1
#define CLASS_CONTEXT_SPECIFIC 2
#define CLASS_PRIVATE          3

const char *PCs[] = {"Primitive", "Constructed"};
const char *short_PCs[] = {"prim", "cons"};

struct t_universal_class_tag {
	const char *name;
	int pc;
};

struct t_universal_class_tag universal_class_tags[] = {
	{"EOC", 0},
	{"BOOLEAN", 0},
	{"INTEGER", 0},
	{"BIT STRING", 2},
	{"OCTET STRING", 2},
	{"NULL", 0},
	{"OBJECT IDENTIFIER", 0},
	{"OBJECT DESCRIPTOR", 2},
	{"EXTERNAL", 1},
	{"REAL", 0},
	{"ENUMERATED", 0},
	{"EMBEDDED PDV", 1},
	{"UTF8String", 2},
	{"RELATIVE-OID", 1},
	{"(reserved)", -1},
	{"(reserved)", -1},
	{"SEQUENCE", 1},          /* UTAG_SEQUENCE */
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
	{"(long form)", -1}        /* UTAG_LONG_FORMAT */
};
#define UTAG_SEQUENCE    16
#define UTAG_LONG_FORMAT 31

const char *length_coding_types[] = {
	"ONE BYTE",   /* LENGTH_CODE_TYPE_ONE */
	"MULTI BYTES" /* LENGTH_CODA_TYPE_MULTI */
};
#define LENGTH_CODE_TYPE_ONE   0
#define LENGTH_CODA_TYPE_MULTI 1

#define assert(b) \
if (!(b)) { \
	fprintf(stderr, "FATAL ERROR: file %s, line %i.\n", __FILE__, __LINE__); \
	exit(-999); \
}

void usage()
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  dder FILE\n\n");
	fprintf(stderr, "Parses FILE according to DER syntax.\n\n");
	exit(-1);
}

ssize_t get_file_size(const char* filename)
{
	struct stat st;
	stat(filename, &st);
	return st.st_size;
}

char *char_to_binary_base_string(char *b, const size_t blen, unsigned char c)
{
	assert(blen >= 9);

	int i;
	for (i = 7; i >= 0; --i) {
		b[i] = (c % 2 == 0 ? '0' : '1');
		c >>= 1;
	}
	b[8] = '\0';
	return b;
}

void myfclose(FILE **F, const char *err, size_t position)
{
	if (err != NULL)
		out("Error: %s, position: %u.\n", err, (unsigned int)position);
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

void out_sequence(size_t offset, char *buf, const unsigned long len, const int string)
{

const unsigned long lbytes = 8;

	const unsigned long hbytes = lbytes / 2;
	if (!string) {
		unsigned long i;
		unsigned long lim = ((len + lbytes - 1) / lbytes) * lbytes;
		if (!lim)
			lim = (unsigned long)lbytes;
		for (i = 0; i < lim; ++i) {
			if (!(i % lbytes)) {
				if (len)
					out("%06x  ", (unsigned int)offset);
				else
					out("        ");
			}
			if (!((i + hbytes) % lbytes))
				out("   ");
			if (i < len)
				out("%02x ", (unsigned char)buf[i]);
			else
				out("   ");
			if (!((i + 1) % lbytes)) {
				if (i + 1 < lim) {
					out("\n");
					offset += lbytes;
				} else {
					out("  ");
				}
			}
		}
	} else {
		out("%06x  ", (unsigned int)offset);
		char c;
		unsigned i;
		for (i = 0; i < len; ++i) {
			c = buf[i];
			if (c < ' ')
				c = '.';
			out("%c", c);
		}
	}
}

int check_position(FILE **F, const size_t offset, const ssize_t remaining_length)
{
	if (*F == NULL)
		return 0;
	if (remaining_length < 0) {
		myfclose(F, "inconsistent items length", offset);
		return 0;
	}
	return 1;
}

void get_tag_name(char *s, const size_t slen, const int tag_class, const int tag_number)
{
	if (tag_class == CLASS_UNIVERSAL) {
		strncpy(s, universal_class_tags[tag_number].name, slen);
		s[slen - 1] = '\0';
	} else {
		snprintf(s, slen, "[ %i ]", tag_number);
	}
}

void parse_identifier(FILE **F, size_t *offset, ssize_t *remaining_length, int *tag_class, int *tag_PC, int *tag_number)
{
	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(F, offset)) == EOF)
		return;

	*tag_class = (c & 0xc0) >> 6;
	*tag_PC = (c & 0x20) >> 5;
	*tag_number = (c & 0x1F);
	int allowed_pc = universal_class_tags[*tag_number].pc;
	if (tag_class == CLASS_UNIVERSAL && allowed_pc >= 0 && allowed_pc <= 1 && *tag_PC != allowed_pc)
		out("Warning: tag number does not match primitive/constructed bit.\n");
	char tag_name[100];
	get_tag_name(tag_name, sizeof(tag_name), *tag_class, *tag_number);

	char buf[1];
	buf[0] = (char)c;
	out_sequence(old_offset, buf, 1, 0);

	if (opt_verbose) {
		char b[9];
		char_to_binary_base_string(b, sizeof(b), (unsigned char)c);
		out("%c%c %c %c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		out_sequence(0, NULL, 0, 0);
		out("^^         Tag class:  %3i -> %s\n", *tag_class, classes[*tag_class]);
		out_sequence(0, NULL, 0, 0);
		out("   ^       Tag type:   %3i -> %s\n", *tag_PC, PCs[*tag_PC]);
		out_sequence(0, NULL, 0, 0);
		out("     ^^^^^ Tag number: %3i -> %s\n", *tag_number, tag_name);
	} else {
		out("%s-%s: %s\n", short_classes[*tag_class], short_PCs[*tag_PC], tag_name);
	}

	if (*tag_number == UTAG_LONG_FORMAT) {
			/* FIXME */
		out("Long tag number format not handled.\n");
		assert(0);
	}

	--(*remaining_length);

	check_position(F, *offset, *remaining_length);
}

void parse_length(FILE **F, size_t *offset, ssize_t *remaining_length, unsigned long *length)
{
	char buf[7];

	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(F, offset)) == EOF)
		return;
	int n = 0;
	buf[0] = (char)c;
	*length = (unsigned long)c;
	int length_type = ((c & 0x80) >> 7);
	if (c & 0x80) {
		n = (c & 0x7F);
		if ((unsigned int)n > sizeof(buf) - 1) {
			myfclose(F, "number of bytes to encode length exceeds maximum", old_offset);
			return;
		} else if (n == 0) {
			myfclose(F, "number of bytes to encode length cannot be null", old_offset);
			return;
		}
		*length = 0;
		int i;
		for (i = 1; i <= n; ++i) {
			if ((c = myfgetc(F, offset)) == EOF)
				return;
			(*length) <<= 8;
			(*length) += (unsigned int)c;
			buf[i] = (char)c;
		}
	}
	out_sequence(old_offset, buf, (unsigned long)n + 1, 0);

	if (opt_verbose) {
		char b[9];
		char_to_binary_base_string(b, sizeof(b), (unsigned char)(buf[0]));
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
	} else {
		out("Length: %lu\n", *length);
	}

	*remaining_length -= (n + 1);

	check_position(F, *offset, *remaining_length);
}

void parse(FILE **F, size_t *offset, ssize_t *remaining_length)
{
	int tag_class;
	int tag_PC;
	int tag_number;

	parse_identifier(F, offset, remaining_length, &tag_class, &tag_PC, &tag_number);
	unsigned long length;
	parse_length(F, offset, remaining_length, &length);

	out_dbg("DEBUG: remaining length = %li.\n", *remaining_length);

	if (tag_number == UTAG_SEQUENCE) {
		while(*remaining_length >= 1) {
			parse(F, offset, remaining_length);
		}
	} else {
		char *buf;
		buf = (char *)malloc(length);
		size_t nbread;
		nbread = fread(buf, 1, (size_t)length, *F);
		size_t old_offset = *offset;
		*offset += nbread;
		*remaining_length -= (ssize_t)nbread;

		out_sequence(old_offset, buf, nbread, !opt_verbose);

		out_dbg("DEBUG: remaining length = %li.\n", *remaining_length);

		if (nbread != length) {
			if (feof(*F)) {
				out("\n");
				myfclose(F, "unexpected end of file", *offset);
			} else {
				out_errno((ssize_t)*offset);
				myfclose(F, NULL, 0);
			}
		} else {
			if (!check_position(F, *offset, *remaining_length)) {
				free(buf);
				return;
			}
		}
		if (*F == NULL) {
			free(buf);
			return;
		}
		free(buf);
		out("\n");
	}
}

int main(int argc, char **argv)
{
	int a = 1;
	if (a < argc) {
		if (!strcasecmp(argv[a], "-v")) {
			opt_verbose = 1;
			a++;
		}
	}
	if (a != argc - 1)
		usage();

	char *f = argv[a];
	out("Parsing file %s.\n", f);

	ssize_t s = get_file_size(f);
	if (s < 0) {
		out_errno(-1);
		exit(-3);
	}
	FILE *F;
	if ((F = fopen(f, "rb")) == NULL) {
		out_errno(-1);
		exit(-2);
	}

	size_t offset = 0;
	parse(&F, &offset, &s);

	if (F != NULL) {
		if (s != 0 || fgetc(F) != EOF) {
			myfclose(&F, "trailing characters in file", offset);
			return -5;
		} else {
			fclose(F);
		}
	} else {
		return -99;
	}
	return 0;
}

