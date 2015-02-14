/* dder.c */

/* Parse der files */

/* Sébastien Millet, February 2015 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#define UNUSED(x) (void)(x)

#define output(args...) \
	printf(args)

const char *classes[] = {"Universal", "Application", "Context-specific", "Private" };

const char *PCs[] = {"Primitive", "Constructed"};

struct t_universal_class_tag {
	const char *name;
	int pc;
};

struct t_universal_class_tag universal_tags[] = {
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
	{"SEQUENCE", 1},					/* UTAG_SEQUENCE */
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
	{"(long form)", 2}
};
const int UTAG_SEQUENCE = 16;

#define assert(b) \
if (!(b)) { \
	fprintf(stderr, "FATAL ERROR: file %s, line %i.\n", __FILE__, __LINE__); \
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

int myfgetc(FILE **F, size_t *offset)
{
	if (*F == NULL)
		return EOF;

	(*offset)++;
	int r = fgetc(*F);
	if (r == EOF) {
		output("Error: unexpected end of file, position %u.\n", (unsigned int)((*offset)-1));
		fclose(*F);
		*F = NULL;
	}
	return r;
}

void out_sequence(size_t offset, char *buf, const unsigned long len)
{

const unsigned long lbytes = 16;
const unsigned long hbytes = lbytes / 2;

	unsigned long i;
	unsigned long lim = ((len + lbytes - 1) / lbytes) * lbytes;
	if (!lim)
		lim = (unsigned long)lbytes;
	for (i = 0; i < lim; ++i) {
		if (!(i % lbytes)) {
			if (len)
				output("%06x  ", (unsigned int)offset);
			else
				output("        ");
		}
		if (!((i + hbytes) % lbytes))
			output("   ");
		if (i < len)
			output("%02x ", (unsigned char)buf[i]);
		else
			output("   ");
		if (!((i + 1) % lbytes)) {
			if (i + 1 < lim) {
				output("\n");
				offset += lbytes;
			} else {
				output("  ");
			}
		}
	}
}

void parse_identifier(FILE *F, size_t *offset, int *tag_class, int *tag_PC, int *tag_number)
{
	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(&F, offset)) == EOF)
		return;
	char b[9];
	char_to_binary_base_string(b, sizeof(b), (unsigned char)c);

	char buf[1];
	buf[0] = (char)c;
	out_sequence(old_offset, buf, 1);
	output("%c%c %c %c%c%c%c%c\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
	*tag_class = (c & 0xc0) >> 6;
	*tag_PC = (c & 0x20) >> 5;
	*tag_number = (c & 0x1F);
	out_sequence(0, NULL, 0);
	output("^^         Tag class:  %3i -> %s\n", *tag_class, classes[*tag_class]);
	out_sequence(0, NULL, 0);
	output("   ^       Tag type:   %3i -> %s\n", *tag_PC, PCs[*tag_PC]);
	out_sequence(0, NULL, 0);
	output("     ^^^^^ Tag number: %3i -> %s\n", *tag_number, universal_tags[*tag_number].name);
	int allowed_pc = universal_tags[*tag_number].pc;
	if (allowed_pc >= 0 && allowed_pc <= 1 && *tag_PC != allowed_pc)
		output("Warning: tag number does not match primitive/constructed bit.\n");
}

void parse_length(FILE *F, size_t *offset, unsigned long *length)
{
	char buf[7];

	int c;
	size_t old_offset = *offset;
	if ((c = myfgetc(&F, offset)) == EOF)
		return;
	int n = 0;
	buf[0] = (char)c;
	*length = (unsigned long)c;
	if (c & 0x80) {
		n = (c & 0x7F);
		if ((unsigned int)n > sizeof(buf) - 1) {
			output("Error: number of bytes to encode length exceeds maximum, position %lu.\n", old_offset);
			fclose(F);
			return;
		} else if (n == 0) {
			output("Error: number of bytes to encode length cannot be null, position %lu.\n", old_offset);
			fclose(F);
			return;
		}
		*length = 0;
		int i;
		for (i = 1; i <= n; ++i) {
			if ((c = myfgetc(&F, offset)) == EOF)
				return;
			(*length) <<= 8;
			(*length) += (unsigned int)c;
			buf[i] = (char)c;
		}
	}
	out_sequence(old_offset, buf, (unsigned long)n + 1);
	output("Length: %lu\n", *length);
}

void parse(FILE *F, size_t *offset, size_t expected_size)
{
	size_t old_offset = *offset;
	int tag_class;
	int tag_PC;
	int tag_number;

	parse_identifier(F, offset, &tag_class, &tag_PC, &tag_number);
	unsigned long length;
	parse_length(F, offset, &length);
	if (F == NULL)
		return;

	if (length + (*offset - old_offset) != expected_size) {
		output("Warning: item length does not match expected size.\n");
	}

	if (tag_number == UTAG_SEQUENCE) {
		parse(F, offset, length);
	} else {
		char *buf;
		buf = (char *)malloc(length);
		if (fread(buf, 1, (size_t)length, F) != length) {
			output("Error: unexpected end of file, position %u.\n", (unsigned int)*offset);
			fclose(F);
			F = NULL;
		}
		free(buf);
		if (F == NULL)
			return;
		out_sequence(*offset, buf, length);
		output("\n");
	}
}

int main(int argc, char **argv)
{
	if (argc != 2)
		usage();

	char *f = argv[1];
	output("Parsing file %s.\n", f);

	ssize_t s = get_file_size(f);
	if (s < 0) {
		fprintf(stderr, "Error: %s.\n", strerror(errno));
		exit(-3);
	}
	FILE *F;
	if ((F = fopen(f, "rb")) == NULL) {
		fprintf(stderr, "Error: %s.\n", strerror(errno));
		exit(-2);
	}

	size_t offset = 0;
	parse(F, &offset, (size_t)s);

	if (F != NULL) {
		if (offset != (size_t)s || fgetc(F) != EOF) {
			output("Error: trailing characters in file.\n");
		}
		fclose(F);
		return 0;
	} else {
		return -99;
	}
}

