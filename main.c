#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

#include "utils/common.h"

char *codefile = NULL;
char *outfile = NULL;
bool silent = false;

String dirname(const String *filename) {
    String dir;
    String_construct(&dir);
    u64 i = filename->count;
    for (; i > 0; --i) {
        if (String_at(filename, i-1) == '/') break;
    }
    if (i == 0) {
        String_push_back(&dir, '.');
    } else {
        for (u64 j = 0; j < i; ++j) {
            String_push_back(&dir, String_at(filename, j));
        }
    }
    String_push_back(&dir, 0);

    return dir;
}

String basename(const String *filename) {
    String name;
    String_construct(&name);
    u64 i = filename->count;
    for (; i > 0; --i) {
        if (String_at(filename, i-1) == '/') break;
    }
    for (; i < filename->count; ++i) {
        String_push_back(&name, String_at(filename, i));
    }
    String_push_back(&name, 0);

    return name;

}

String read_file(char *filename) {
    FILE *fp = fopen(filename, "rb");
    String file_data;
    String_construct(&file_data);
    fseek(fp, 0L, SEEK_END);
    file_data.count = ftell(fp);
    rewind(fp);
    file_data.data = malloc((file_data.count+1) * sizeof(char));
    size_t actually_read = fread(file_data.data, sizeof(char), file_data.count, fp);
    if (actually_read != file_data.count) {
        error(-1, "Error reading file.");
    }
    fclose(fp);
    file_data.data[file_data.count] = 0;
    return file_data;
}

void parse_args(int argc, char **argv) {
    for (s64 i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            ++i;
            outfile = argv[i];
        } else if (strcmp(argv[i], "-s") == 0) {
            silent = true;
        } else {
            codefile = argv[i];
        }
    }
    if (codefile == NULL) {
        error(0, "Error: No file!");
    }
}

String preprocess(String *in_code);

void remove_comments(const String *in_code, String *out_code) {
    enum {
        NORMAL = 0,
        COMMENT = 1,
        BLOCK_COMMENT = 2,
        LINE_COMMENT = 3,
    } state = NORMAL;
    u64 line = 1;
    for (u64 i = 0; i < in_code->count; ++i) {
        char c = String_at(in_code, i);
        switch (state) {
            case NORMAL: {
                if (c == '/') {
                    state = COMMENT;
                } else {
                    String_push_back(out_code, c);
                }
                break;
            }
            case COMMENT: {
                if (c == '/') {
                    state = LINE_COMMENT;
                } else if (c == '*') {
                    state = BLOCK_COMMENT;
                } else {
                    String_push_back(out_code, '/');
                    String_push_back(out_code, c);
                    state = NORMAL;
                }
                break;
            }
            case BLOCK_COMMENT: {
                // NOTE(mdizdar): by this logic /*/ is a valid block comment
                // who gives a shit tho
                if (c == '/' && String_at(in_code, i-1) == '*') {
                    String_push_back(out_code, ' ');
                    state = NORMAL;
                }
                break;
            }
            case LINE_COMMENT: {
                break;
            }
        }
        if (String_at(in_code, i) == '\n') {
            if (state == LINE_COMMENT) {
                String_push_back(out_code, '\n');
                state = NORMAL;
            }
            ++line;
        }
    }
    if (state == BLOCK_COMMENT) {
        error(line, "koji ti je kurac");
    }
}

void do_directives(const String *in_code, String *out_code) {
    enum {
        FIRST = 0,
        DIRECTIVE = 1,
        KURAC = 5,
    } state = FIRST;
    u64 line = 1;
    for (u64 i = 0; i < in_code->count; ++i) {
        char c = String_at(in_code, i);
        switch (state) {
            case FIRST: {
                if (c == '#') {
                    state = DIRECTIVE;
                } else if (!isspace(c)) {
                    state = KURAC;
                }
                break;
            }
            case DIRECTIVE: {
                for (int j = 0; j < 7; ++j) {
                    if (tolower(String_at(in_code, i+j)) != "include"[j]) {
                        error(line, "koji kurac je ovaj include");
                    }
                }
                char after = String_at(in_code, i+7);
                
                if (isspace(after) || after == '<' || after == '"') {
                    // yay include
                    String filename;
                    String_construct(&filename);
                    for (; String_at(in_code, i) != '<' && String_at(in_code, i) != '"'; ++i);
                    for (++i; String_at(in_code, i) != '>' && String_at(in_code, i) != '"'; ++i) {
                        String_push_back(&filename, String_at(in_code, i));
                    }
                    
                    String included_code = preprocess(&filename);

                    for (u64 i = 0; i < included_code.count; ++i) {
                        String_push_back(out_code, String_at(&included_code, i));
                    }

                    state = KURAC;

                    String_destruct(&included_code);
                    String_destruct(&filename);
                }
                break;
            }
            case KURAC: {
                String_push_back(out_code, c);
                break;
            }
        }
        if (c == '\n') {
            line += 1;
            state = KURAC;
        }
    }
}

String preprocess(String *filename) {
    char olddir[512];
    getcwd(olddir, 512);
    String directory = dirname(filename);
    String base = basename(filename);
    chdir(directory.data);
    String in_code = read_file(base.data);

    String out_code;
    String_construct(&out_code);

    remove_comments(&in_code, &out_code);
    String_destruct(&in_code);
    in_code = out_code;
    
    String_construct(&out_code);
    do_directives(&in_code, &out_code);

    chdir(olddir);

    String_destruct(&directory);
    String_destruct(&base);
    return out_code;
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    
    String filename = {
        .data = codefile,
        .count = strlen(codefile),
        .capacity = strlen(codefile)
    };
    String out_code = preprocess(&filename);

    if (outfile != NULL) {
        FILE *fp = fopen(outfile, "wb");
        fwrite(out_code.data, sizeof(char), out_code.count, fp);
        fclose(fp);
    } else {
        write(1, out_code.data, out_code.count);
    }

    return 0;
}
