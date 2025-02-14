#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char bool;
#define true 1
#define false 0

#define BLOCK_SIZE 1024
#define MAX_FILES 128
#define MAX_FILENAME_LEN 64

typedef struct {
    unsigned int disk_size;
    unsigned short block_size;
    unsigned int num_blocks;
    unsigned long first_data_block;
    unsigned short num_files;
    unsigned short max_files;
} DiskMetadata;

typedef struct {
    char file_name[MAX_FILENAME_LEN];
    unsigned int file_size;
    unsigned int first_block;
    unsigned char file_type;
} Inode;

unsigned int count_blocks(unsigned int disk_size_bytes) {
    unsigned int bitmap_size_bytes;
    unsigned int inode_bitmap_size_bytes = MAX_FILES * sizeof(bool);
    unsigned long reserved_space;
    unsigned int num_blocks = (disk_size_bytes - sizeof(DiskMetadata) - inode_bitmap_size_bytes - (MAX_FILES * sizeof(Inode))) / BLOCK_SIZE;
    unsigned int prev_num_blocks = 0;

    while (num_blocks != prev_num_blocks) {
        prev_num_blocks = num_blocks;
        bitmap_size_bytes = num_blocks * sizeof(bool);
        reserved_space = sizeof(DiskMetadata) + bitmap_size_bytes + inode_bitmap_size_bytes + (MAX_FILES * sizeof(Inode));

        num_blocks = (disk_size_bytes - reserved_space) / BLOCK_SIZE;
    }
    return num_blocks;
}

void initialize_disk(const char *filename, unsigned int disk_size_mb) {
    FILE *disk;
    unsigned int disk_size_bytes;
    unsigned int num_blocks;
    unsigned int block_bitmap_size_bytes;
    unsigned int inode_bitmap_size_bytes;
    unsigned long first_data_block;
    DiskMetadata metadata;
    bool *block_bitmap;
    bool *inode_bitmap;
    Inode *inode_catalog;
    unsigned int remaining_bytes;
    unsigned char zero_block[BLOCK_SIZE];
    unsigned int i;

    disk = fopen(filename, "wb");
    if (!disk) {
        perror("Failed to create disk file");
        exit(EXIT_FAILURE);
    }

    disk_size_bytes = disk_size_mb * 1024 * 1024;
    num_blocks = count_blocks(disk_size_bytes);

    block_bitmap_size_bytes = num_blocks * sizeof(bool);
    inode_bitmap_size_bytes = MAX_FILES * sizeof(bool);
    first_data_block = sizeof(DiskMetadata) + block_bitmap_size_bytes + inode_bitmap_size_bytes + (MAX_FILES * sizeof(Inode));

    metadata.disk_size = disk_size_mb;
    metadata.block_size = BLOCK_SIZE;
    metadata.num_blocks = num_blocks;
    metadata.first_data_block = first_data_block;
    metadata.num_files = 0;
    metadata.max_files = MAX_FILES;

    block_bitmap = (bool *)calloc(num_blocks, sizeof(bool));
    inode_bitmap = (bool *)calloc(MAX_FILES, sizeof(bool));
    inode_catalog = (Inode *)calloc(MAX_FILES, sizeof(Inode));

    if (!block_bitmap || !inode_bitmap || !inode_catalog) {
        perror("Failed to allocate memory");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    fwrite(&metadata, sizeof(DiskMetadata), 1, disk);
    fwrite(block_bitmap, sizeof(bool), num_blocks, disk);
    fwrite(inode_bitmap, sizeof(bool), MAX_FILES, disk);
    fwrite(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    remaining_bytes = disk_size_bytes - first_data_block;
    memset(zero_block, 0, BLOCK_SIZE);
    for (i = 0; i < remaining_bytes / BLOCK_SIZE; i++) {
        fwrite(zero_block, BLOCK_SIZE, 1, disk);
    }

    printf("Disk initialized successfully.\n");
    printf("Metadata: disk size = %u MB, number of blocks = %u\n", disk_size_mb, num_blocks);
    printf("First data block starts at offset = %lu bytes\n", first_data_block);

    free(block_bitmap);
    free(inode_bitmap);
    free(inode_catalog);
    fclose(disk);
}

void copy_file_to_disk(const char *disk_filename, const char *source_filename) {
    FILE *disk;
    FILE *source;
    DiskMetadata metadata;
    bool *block_bitmap;
    bool *inode_bitmap;
    int inode_index;
    unsigned int free_block_count;
    unsigned int file_size;
    unsigned int blocks_needed;
    unsigned int current_block;
    unsigned int i;
    unsigned int next_block;
    unsigned char buffer[BLOCK_SIZE];
    unsigned int *free_blocks = NULL;
    Inode inode;

    if (strlen(source_filename) >= MAX_FILENAME_LEN) {
        fprintf(stderr, "File name '%s' is too long (maximum length is %d characters).\n", 
                source_filename, MAX_FILENAME_LEN - 1);
        return;
    }

    disk = fopen(disk_filename, "r+b");
    if (!disk) {
        perror("Failed to open disk file");
        return;
    }

    source = fopen(source_filename, "rb");
    if (!source) {
        perror("Failed to open source file");
        fclose(disk);
        return;
    }

    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    if (metadata.num_files >= metadata.max_files) {
        fprintf(stderr, "No space for a new file in the directory.\n");
        fclose(disk);
        fclose(source);
        return;
    }

    block_bitmap = (bool *)malloc(metadata.num_blocks * sizeof(bool));
    inode_bitmap = (bool *)malloc(MAX_FILES * sizeof(bool));
    if (!block_bitmap || !inode_bitmap) {
        fprintf(stderr, "Failed to allocate memory for bitmaps.\n");
        fclose(disk);
        fclose(source);
        free(block_bitmap);
        free(inode_bitmap);
        return;
    }

    free_blocks = malloc(metadata.num_blocks * sizeof(unsigned int));
    if (!free_blocks) {
        fprintf(stderr, "Failed to allocate memory for free blocks array.\n");
        free(block_bitmap);
        free(inode_bitmap);
        fclose(disk);
        fclose(source);
        return;
    }

    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fread(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fread(inode_bitmap, sizeof(bool), MAX_FILES, disk);

    inode_index = -1;
    for (i = 0; i < MAX_FILES; i++) {
        if (!inode_bitmap[i]) {
            inode_index = i;
            break;
        }
    }
    if (inode_index == -1) {
        fprintf(stderr, "No free inode.\n");
        free(block_bitmap);
        free(inode_bitmap);
        fclose(disk);
        fclose(source);
        return;
    }

    free_block_count = 0;
    for (i = 0; i < metadata.num_blocks; i++) {
        if (!block_bitmap[i]) {
            free_blocks[free_block_count++] = i;
        }
    }

    fseek(source, 0, SEEK_END);
    file_size = ftell(source);
    rewind(source);

    blocks_needed = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocks_needed > free_block_count) {
        fprintf(stderr, "Not enough space on disk for this file.\n");
        free(block_bitmap);
        free(inode_bitmap);
        fclose(disk);
        fclose(source);
        return;
    }

    memset(&inode, 0, sizeof(Inode));
    strncpy(inode.file_name, source_filename, MAX_FILENAME_LEN - 1);
    inode.file_size = file_size;
    inode.first_block = free_blocks[0];
    inode.file_type = (source_filename[0] == '.') ? 1 : 0;

    current_block = inode.first_block;
    for (i = 0; i < blocks_needed; i++) {
        next_block = (i + 1 < blocks_needed) ? free_blocks[i + 1] : (unsigned int)-1;

        block_bitmap[current_block] = true;

        memset(buffer, 0, BLOCK_SIZE);
        fread(buffer, 1, BLOCK_SIZE, source);
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE, SEEK_SET);
        fwrite(buffer, 1, BLOCK_SIZE, disk);

        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE + BLOCK_SIZE - sizeof(int), SEEK_SET);
        fwrite(&next_block, sizeof(int), 1, disk);

        current_block = next_block;
    }

    inode_bitmap[inode_index] = true;
    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fwrite(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fwrite(inode_bitmap, sizeof(bool), MAX_FILES, disk);

    fseek(disk, sizeof(DiskMetadata) + metadata.num_blocks + MAX_FILES, SEEK_SET);
    fseek(disk, inode_index * sizeof(Inode), SEEK_CUR);
    fwrite(&inode, sizeof(Inode), 1, disk);

    metadata.num_files++;
    fseek(disk, 0, SEEK_SET);
    fwrite(&metadata, sizeof(DiskMetadata), 1, disk);

    free(free_blocks);
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
    fclose(source);

    printf("File '%s' copied to virtual disk.\n", source_filename);
}

void copy_file_from_disk(const char *disk_filename, const char *output_filename) {
    FILE *disk;
    FILE *output;
    DiskMetadata metadata;
    bool *inode_bitmap;
    Inode *inode_catalog;
    Inode *file_inode = NULL;
    unsigned int current_block;
    unsigned int bytes_remaining;
    unsigned char buffer[BLOCK_SIZE];
    unsigned int bytes_to_write;
    int i;

    disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Failed to open disk file");
        return;
    }

    fseek(disk, 0, SEEK_SET);
    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    inode_bitmap = (bool *)malloc(MAX_FILES * sizeof(bool));
    inode_catalog = (Inode *)malloc(MAX_FILES * sizeof(Inode));
    if (!inode_bitmap || !inode_catalog) {
        perror("Failed to allocate memory");
        fclose(disk);
        free(inode_bitmap);
        free(inode_catalog);
        return;
    }

    fseek(disk, sizeof(DiskMetadata) + metadata.num_blocks, SEEK_SET);
    fread(inode_bitmap, sizeof(bool), MAX_FILES, disk);
    fread(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    for (i = 0; i < MAX_FILES; i++) {
        if (inode_bitmap[i] && strcmp(inode_catalog[i].file_name, output_filename) == 0) {
            file_inode = &inode_catalog[i];
            break;
        }
    }

    if (!file_inode) {
        printf("File '%s' not found on disk.\n", output_filename);
        free(inode_bitmap);
        free(inode_catalog);
        fclose(disk);
        return;
    }

    output = fopen(output_filename, "wb");
    if (!output) {
        perror("Failed to create output file");
        free(inode_bitmap);
        free(inode_catalog);
        fclose(disk);
        return;
    }

    current_block = file_inode->first_block;
    bytes_remaining = file_inode->file_size;

    while (current_block != (unsigned int)-1) {
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE, SEEK_SET);
        fread(buffer, 1, BLOCK_SIZE, disk);

        bytes_to_write = (bytes_remaining < BLOCK_SIZE) ? bytes_remaining : BLOCK_SIZE;
        fwrite(buffer, 1, bytes_to_write, output);

        bytes_remaining -= bytes_to_write;
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE + BLOCK_SIZE - sizeof(int), SEEK_SET);
        fread(&current_block, sizeof(int), 1, disk);
    }

    fclose(output);
    free(inode_bitmap);
    free(inode_catalog);
    fclose(disk);

    printf("File '%s' copied from virtual disk.\n", output_filename);
}

void delete_file_from_disk(const char *disk_filename, const char *file_name) {
    FILE *disk;
    DiskMetadata metadata;
    bool *block_bitmap;
    bool *inode_bitmap;
    Inode *inode_catalog;
    size_t offset_to_block_bitmap, offset_to_inode_bitmap, offset_to_inode_catalog;
    unsigned int current_block, next_block;
    int inode_index = -1;
    unsigned int i;

    disk = fopen(disk_filename, "r+b");
    if (!disk) {
        perror("Nie udalo sie");
        exit(EXIT_FAILURE);
    }

    fread(&metadata, sizeof(DiskMetadata), 1, disk);


    offset_to_block_bitmap = sizeof(DiskMetadata);
    offset_to_inode_bitmap = offset_to_block_bitmap + metadata.num_blocks * sizeof(bool);
    offset_to_inode_catalog = offset_to_inode_bitmap + MAX_FILES * sizeof(bool);

    
    block_bitmap = (bool *)malloc(metadata.num_blocks * sizeof(bool));
    inode_bitmap = (bool *)malloc(MAX_FILES * sizeof(bool));
    inode_catalog = (Inode *)malloc(MAX_FILES * sizeof(Inode));
    if (!block_bitmap || !inode_bitmap || !inode_catalog) {
        perror("Nie udalo sie");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    fseek(disk, offset_to_block_bitmap, SEEK_SET);
    fread(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fread(inode_bitmap, sizeof(bool), MAX_FILES, disk);
    fread(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    for (i = 0; i < MAX_FILES; i++) {
        if (inode_bitmap[i] && strcmp(inode_catalog[i].file_name, file_name) == 0) {
            inode_index = i;
            break;
        }
    }

    if (inode_index == -1) {
        fprintf(stderr, "Plik '%s' nie istnieje na dysku.\n", file_name);
        free(block_bitmap);
        free(inode_bitmap);
        free(inode_catalog);
        fclose(disk);
        return;
    }

    current_block = inode_catalog[inode_index].first_block;
    while (current_block != (unsigned int)-1) {
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE + BLOCK_SIZE - sizeof(int), SEEK_SET);
        fread(&next_block, sizeof(int), 1, disk);
        block_bitmap[current_block] = false;
        current_block = next_block;
    }

    inode_bitmap[inode_index] = false;
    memset(&inode_catalog[inode_index], 0, sizeof(Inode));

    metadata.num_files--;

    fseek(disk, 0, SEEK_SET);
    fwrite(&metadata, sizeof(DiskMetadata), 1, disk);
    fseek(disk, offset_to_block_bitmap, SEEK_SET);
    fwrite(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fwrite(inode_bitmap, sizeof(bool), MAX_FILES, disk);
    fwrite(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    free(block_bitmap);
    free(inode_bitmap);
    free(inode_catalog);
    fclose(disk);

    printf("File '%s' was removed from virtual disk.\n", file_name);
}


void display_block_bitmap(const char *disk_filename) {
    FILE *disk;
    DiskMetadata metadata;
    bool *block_bitmap;
    unsigned int i;

    disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Nie udało sie");
        exit(EXIT_FAILURE);
    }

    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    block_bitmap = (bool *)malloc(metadata.num_blocks * sizeof(bool));
    if (!block_bitmap) {
        perror("Nie udało sie");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fread(block_bitmap, sizeof(bool), metadata.num_blocks, disk);

    printf("Indexes of occupied blocks:\n");
    for (i = 0; i < metadata.num_blocks; i++) {
        if (block_bitmap[i]) {
            printf("Block %u is occupied\n", i);
        }
    }

    free(block_bitmap);
    fclose(disk);
    printf("Others blocks are free\n");
}


void list_files_on_disk(const char *disk_filename, bool show_hidden) {
    FILE *disk;
    DiskMetadata metadata;
    Inode *inode_catalog;
    unsigned int i;

    disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Nie udalo sie");
        exit(EXIT_FAILURE);
    }

    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    fseek(disk, sizeof(DiskMetadata) + metadata.num_blocks * sizeof(bool) + MAX_FILES * sizeof(bool), SEEK_SET);

    inode_catalog = (Inode *)malloc(MAX_FILES * sizeof(Inode));
    if (!inode_catalog) {
        perror("Nie udalo sie");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    fread(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    printf("%-40s %-10s %-10s\n", "Nazwa pliku", "Rozmiar", "Pierwszy blok");
    printf("---------------------------------------------\n");


    for (i = 0; i < MAX_FILES; i++) {
        if (inode_catalog[i].file_name[0] != '\0') {
            if (inode_catalog[i].file_type == 1 && !show_hidden) {
                continue;
            }
            printf("%-40s %-10u %-10u\n",
                inode_catalog[i].file_name, 
                inode_catalog[i].file_size, 
                inode_catalog[i].first_block);
        }
    }

    free(inode_catalog);
    fclose(disk);
}



int main(int argc, char *argv[]) {
    unsigned int disk_size_mb;
    bool show_hidden;
    char disk_filename[64] = "vd.bin";
    char filename[64];
    int choice;

    if (argc < 5) {
        printf("Za malo argumentów.\n");
        return 1;
    }

    disk_size_mb = atoi(argv[2]);
    strncpy(disk_filename, argv[3], MAX_FILENAME_LEN - 1);
    disk_filename[MAX_FILENAME_LEN- 1] = '\0';

    if (atoi(argv[1]) == 1) {
        initialize_disk(disk_filename, disk_size_mb);
        return 0;
    }

    show_hidden = atoi(argv[4]);
    choice = atoi(argv[5]);

    switch (choice) {
        case 1:
            if (argc < 7) {
                printf("Podaj nazwe pliku do skopiowania na dysk.\n");
                return 1;
            }
            strncpy(filename, argv[6], MAX_FILENAME_LEN - 1);
            filename[MAX_FILENAME_LEN - 1] = '\0';
            copy_file_to_disk(disk_filename, filename);
            break;

        case 2:
            if (argc < 7) {
                printf("Podaj nazwe pliku do skopiowania z dysku.\n");
                return 1;
            }
            strncpy(filename, argv[6], MAX_FILENAME_LEN - 1);
            filename[MAX_FILENAME_LEN - 1] = '\0';
            copy_file_from_disk(disk_filename, filename);
            break;

        case 3:
            display_block_bitmap(disk_filename);
            break;

        case 4:
            list_files_on_disk(disk_filename, show_hidden);
            break;

        case 5:
            if (argc < 7) {
                printf("Podaj nazwe pliku do usuniecia z dysku.\n");
                return 1;
            }
            strncpy(filename, argv[6], MAX_FILENAME_LEN - 1);
            filename[MAX_FILENAME_LEN - 1] = '\0';
            delete_file_from_disk(disk_filename, filename);
            break;

        case 6:
            return 0;

        default:
            printf("Nieprawidlowy wybór.\n");
            return 1;
    }

    return 0;
}


/*
int main() {
    unsigned int disk_size_mb;
    int choice;
    bool show_hidden = false;
    char disk_filename[64] = "vd.bin";
    char filename[64];

    printf("Podaj rozmiar dysku w MB: ");
    scanf("%u", &disk_size_mb);
    initialize_disk(disk_filename, disk_size_mb);

    while (1) {
        printf("\nWybierz czynność:\n");
        printf("1. Skopiuj plik na dysk\n");
        printf("2. Skopiuj plik z dysku\n");
        printf("3. Wyświetl bitmapę bloków\n");
        printf("4. Wylistuj pliki na dysku\n");
        printf("5. Usun plik z dysku wirtualnego\n");
        printf("0. Zakończ program\n");
        printf("Twój wybór: ");
        scanf("%d", &choice);

        getchar();

        switch (choice) {
            case 1:
                printf("Podaj nazwę pliku do skopiowania na dysk: ");
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                copy_file_to_disk(disk_filename, filename);
                break;

            case 2:
                printf("Podaj nazwę pliku do skopiowania z dysku: ");
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
                copy_file_from_disk(disk_filename, filename);
                break;

            case 3:
                display_block_bitmap(disk_filename);
                break;

            case 0:
                printf("Zakończono program.\n");
                exit(0);

            case 4:
                printf("Czy chcesz wyświetlić również pliki ukryte? (1 = tak, 0 = nie): ");
                unsigned int hidden_choice;
                scanf("%u", &hidden_choice);
                show_hidden = (hidden_choice == 1);
                list_files_on_disk(disk_filename, show_hidden);
                break;

            case 5:
                printf("Jaki plik chcesz usunac z dysku?: ");
                char file_to_delete[64];
                fgets(file_to_delete, sizeof(file_to_delete), stdin);
                file_to_delete[strcspn(file_to_delete, "\n")] = 0;
                delete_file_from_disk(disk_filename, file_to_delete);
                break;

            default:
                printf("Nieprawidłowy wybór. Spróbuj ponownie.\n");
        }
    }
    return 0;
}*/

