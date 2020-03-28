#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>

using namespace std;

#define LATM_invalid(header) ((header)[0] != 0x56 || ((header)[1] & 0xE0) != 0xE0)
#define LATM_channel(header) (((header)[6] & 0x78) >> 3)
#define LATM_length(header) (((((header)[1] & 0x1F) << 8) | (header)[2]) + 3)
#define BUFFER_LEN (1<<24)

char* name_prefix;
char out_filename[10240] = {0};

size_t total = 0;

void show_progress(size_t progress) {
    printf("%7d/%d MB %.2lf%%     \r", progress >> 20, total >> 20, progress * 100.0 / total);
}

void sync_signal(ifstream& input) {
    unsigned char buffer[1048576] = {0};
    unsigned char* header;
    input.read((char*) buffer, sizeof(buffer));
    for(header = buffer; header < buffer + sizeof(buffer)-(1<<14); header++) {
        // Check current syncword
        if(LATM_invalid(header))
            continue;
        int length = LATM_length(header);
        // Check next syncword by length
        if(LATM_invalid(header + length))
            continue;
        if(header > buffer)
            printf("Syncing to %d bytes\n", header - buffer);
        input.seekg(header - buffer, ios::beg);
        return;
    }
    puts("Unable to sync in first megabytes, halt.");
    exit(-1);
}

void split_latm(const char * latm_filename) {
    unsigned int file_index = 0;
    unsigned int block_index = 0;
    int last_channel = -1;
    int len = strlen(latm_filename) - 4;
    name_prefix = new char[len];
    strncpy(name_prefix, latm_filename, len - 1);
    name_prefix[len - 1] = 0;
    printf("Opening %s\n", latm_filename);
    ifstream input(latm_filename, ios::in | ios::binary);
    ofstream output;
    unsigned char header[7];
    char* buffer = new char[BUFFER_LEN];
    int buffer_valid_length;
    int pending_check_begin;
    size_t stream_pos;
    int i = 0;
    int force_cut = 0;
    // Get total
    auto beginning = input.tellg();
    input.seekg(0, ios::end);
    total = input.tellg() - beginning;
    input.seekg(0, ios::beg);
    sync_signal(input);
    stream_pos = input.tellg() - beginning;
    while(!input.eof() && stream_pos <= total) {
        reread_data:
        stream_pos = input.tellg() - beginning;
        buffer_valid_length = BUFFER_LEN;
        if(stream_pos + buffer_valid_length > total)
            buffer_valid_length = total - stream_pos;

        input.read(buffer, buffer_valid_length);
        input.peek();
        pending_check_begin = 0;

        while(true) {
            unsigned char* header = (unsigned char*)buffer + pending_check_begin;
            if(pending_check_begin + 7 > buffer_valid_length)
                break;
            int latm_len = LATM_length(header);

            // Last full block?
            bool fullblock = pending_check_begin + latm_len == buffer_valid_length && input.eof();
            bool lastblock = pending_check_begin + latm_len + 4 > buffer_valid_length && input.eof();

            if (lastblock && !force_cut) {
                pending_check_begin = buffer_valid_length;
                break;
            }

            if(!fullblock && (pending_check_begin + latm_len + 4 > buffer_valid_length))
                break;

            if(LATM_invalid(header)) {
                printf("Data is corrupted at %ld+%d\n", stream_pos, pending_check_begin);
                printf("Sync word is %X%X, unable to sync.\n", header[0], header[1]);
                return;
            }

            int force_cut_here = force_cut;

            if(!lastblock && LATM_invalid(header + latm_len)) {
                printf("Data is corrupted at %ld+%d\n", stream_pos, pending_check_begin + latm_len);
                printf("Sync word is %X%X, re-syncing.\n", header[latm_len], header[latm_len+1]);
                unsigned char* next_header = header + 2;
                while(++next_header < (unsigned char*)buffer + buffer_valid_length - 2) {
                    if(!LATM_invalid(next_header)) {
                        latm_len = next_header - header;
                        force_cut = 1;
                        printf("Re-syncing at %ld+%d\n", stream_pos, pending_check_begin + latm_len);
                        break;
                    }
                }
            }

            if(LATM_channel(header) != last_channel || force_cut_here) {
                force_cut = 0;
                // Channel configuration has changed, writing to new destination
                input.seekg(pending_check_begin - buffer_valid_length, ios_base::cur);

                if(output.is_open()) {
                    output.write(buffer, pending_check_begin);
                    output.close();
                }
                last_channel = LATM_channel(header);
                sprintf(out_filename, "%s_%d_%dcc.latm", name_prefix, ++file_index, LATM_channel(header));
                printf("Saving to %s ...                    \n", out_filename);
                output.open(out_filename, ios::out | ios::binary | ios::trunc);
                goto reread_data;
            }
            pending_check_begin += latm_len;
        }

        if(output.is_open())
            output.write(buffer, pending_check_begin);
        if(input.eof())
            break;
        input.seekg(pending_check_begin - buffer_valid_length, ios_base::cur);

        // if((block_index & 0x1FFF) == 0)
            show_progress(input.tellg() - beginning);
        // block_index++;
    }
    show_progress(total);
    if(output.is_open())
        output.close();
    delete[] buffer;
}

void help(const char * argv[]) {
    printf("%s <file.latm> ...\n", argv[0]);
    puts("");
    puts("Split LATM AAC with multiple channel configurations into segments.");
    puts("");
    puts("CAUTION! 'file_X_Ycc.latm' will be overwritten without confirmation!");
    puts("");
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        help(argv);
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        const char* latm_filename = argv[i];
        const char* extension = latm_filename + strlen(latm_filename) - 5;
        if (strcmp(extension, ".latm") != 0) {
            printf("Only .latm file is accepted, %s given.\n", latm_filename);
            return -2;
        }

        split_latm(latm_filename);
    }

    return 0;
}
