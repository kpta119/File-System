#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char bool;
#define true 1;
#define false 0;

#define BLOCK_SIZE 1024          // Rozmiar jednego bloku w bajtach
#define MAX_FILES 128            // Maksymalna liczba plików w katalogu
#define MAX_FILENAME_LEN 64      // Maksymalna długość nazwy pliku

// Struktura metadanych dysku
typedef struct {
    unsigned int disk_size;          // Rozmiar dysku w MB
    unsigned short block_size;       // Rozmiar bloku w bajtach
    unsigned int num_blocks;         // Liczba bloków
    unsigned long first_data_block;  // Adres pierwszego bloku danych
    unsigned short num_files;        // Liczba plików w katalogu
    unsigned short max_files;        // Maksymalna liczba plików
} DiskMetadata;

// Struktura pojedynczego Inode
typedef struct {
    char file_name[MAX_FILENAME_LEN]; // Nazwa pliku
    unsigned int file_size;           // Rozmiar pliku w bajtach
    unsigned int first_block;         // Indeks pierwszego bloku danych
    unsigned char file_type;          // Typ pliku (0 = zwykły, 1 = ukryty)
} Inode;


unsigned int count_blocks(unsigned int disk_size_bytes) {
    unsigned int bitmap_size_bytes;
    unsigned int inode_bitmap_size_bytes = MAX_FILES * sizeof(bool);
    unsigned long reserved_space;
    unsigned int num_blocks = (disk_size_bytes - sizeof(DiskMetadata) - inode_bitmap_size_bytes - (MAX_FILES * sizeof(Inode))) / BLOCK_SIZE;
    unsigned int prev_num_blocks = 0;

    while (num_blocks != prev_num_blocks) {
        prev_num_blocks = num_blocks;

        // Oblicz rozmiar bitmapy bloków jako tablicy bool
        bitmap_size_bytes = num_blocks * sizeof(bool);

        // Oblicz całkowitą zajętą przestrzeń
        reserved_space = sizeof(DiskMetadata) + bitmap_size_bytes + inode_bitmap_size_bytes + (MAX_FILES * sizeof(Inode));

        num_blocks = (disk_size_bytes - reserved_space) / BLOCK_SIZE;
    }
    return num_blocks;
}


void initialize_disk(const char *filename, unsigned int disk_size_mb) {
    FILE *disk = fopen(filename, "wb");
    if (!disk) {
        perror("Nie udało się utworzyć pliku dysku");
        exit(EXIT_FAILURE);
    }

    unsigned int disk_size_bytes = disk_size_mb * 1024 * 1024;
    unsigned int num_blocks = count_blocks(disk_size_bytes);

    unsigned int block_bitmap_size_bytes = num_blocks * sizeof(bool);
    unsigned int inode_bitmap_size_bytes = MAX_FILES * sizeof(bool);
    unsigned long first_data_block = sizeof(DiskMetadata) + block_bitmap_size_bytes + inode_bitmap_size_bytes + (MAX_FILES * sizeof(Inode));

    // Inicjalizacja metadanych
    DiskMetadata metadata = {
        .disk_size = disk_size_mb,
        .block_size = BLOCK_SIZE,
        .num_blocks = num_blocks,
        .first_data_block = first_data_block,
        .num_files = 0,
        .max_files = MAX_FILES,
    };

    // Alokowanie pamięci dla bitmap i katalogu i-odów
    bool *block_bitmap = calloc(num_blocks, sizeof(bool));
    bool *inode_bitmap = calloc(MAX_FILES, sizeof(bool));
    Inode *inode_catalog = calloc(MAX_FILES, sizeof(Inode));

    if (!block_bitmap || !inode_bitmap || !inode_catalog) {
        perror("Nie udało się zaalokować pamięci");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    // Zapis metadanych, bitmap i katalogu do pliku
    fwrite(&metadata, sizeof(DiskMetadata), 1, disk);
    fwrite(block_bitmap, sizeof(bool), num_blocks, disk);
    fwrite(inode_bitmap, sizeof(bool), MAX_FILES, disk);
    fwrite(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    // Wypełnienie pozostałego miejsca (obszar danych) zerami
    unsigned int remaining_bytes = disk_size_bytes - first_data_block;
    unsigned char zero_block[BLOCK_SIZE] = {0};
    for (unsigned int i = 0; i < remaining_bytes / BLOCK_SIZE; i++) {
        fwrite(zero_block, BLOCK_SIZE, 1, disk);
    }

    printf("Dysk został pomyślnie zainicjalizowany.\n");
    printf("Metadane: rozmiar dysku = %u MB, liczba bloków = %u\n", disk_size_mb, num_blocks);
    printf("Pierwszy blok danych zaczyna się na offset = %lu bajtów\n", first_data_block);

    // Sprzątanie
    free(block_bitmap);
    free(inode_bitmap);
    free(inode_catalog);
    fclose(disk);
}

void copy_file_to_disk(const char *disk_filename, const char *source_filename) {
    FILE *disk = fopen(disk_filename, "r+b");
    if (!disk) {
        perror("Nie udało się otworzyć pliku dysku");
        exit(EXIT_FAILURE);
    }

    FILE *source = fopen(source_filename, "rb");
    if (!source) {
        perror("Nie udało się otworzyć pliku źródłowego");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    // Wczytanie metadanych
    DiskMetadata metadata;
    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    // Sprawdzenie miejsca na nowe pliki
    if (metadata.num_files >= metadata.max_files) {
        fprintf(stderr, "Brak miejsca na nowy plik w katalogu.\n");
        fclose(disk);
        fclose(source);
        return;
    }

    // Wczytanie bitmapy bloków i i-odów
    bool *block_bitmap = malloc(metadata.num_blocks);
    bool *inode_bitmap = malloc(MAX_FILES);
    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fread(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fread(inode_bitmap, sizeof(bool), MAX_FILES, disk);

    // Znalezienie wolnego inoda
    int inode_index = -1;
    for (unsigned int i = 0; i < MAX_FILES; i++) {
        if (!inode_bitmap[i]) {
            inode_index = i;
            break;
        }
    }
    if (inode_index == -1) {
        fprintf(stderr, "Brak wolnych i-odów.\n");
        free(block_bitmap);
        free(inode_bitmap);
        fclose(disk);
        fclose(source);
        return;
    }

    // Znalezienie pustych bloków
    unsigned int free_blocks[metadata.num_blocks];
    unsigned int free_block_count = 0;
    for (unsigned int i = 0; i < metadata.num_blocks; i++) {
        if (!block_bitmap[i]) {
            free_blocks[free_block_count++] = i;
        }
    }

    // Sprawdzenie dostępnego miejsca
    fseek(source, 0, SEEK_END);
    unsigned int file_size = ftell(source);
    rewind(source);

    unsigned int blocks_needed = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE; // Liczba potrzebnych bloków
    if (blocks_needed > free_block_count) {
        fprintf(stderr, "Brak miejsca na dysku na ten plik.\n");
        free(block_bitmap);
        free(inode_bitmap);
        fclose(disk);
        fclose(source);
        return;
    }

    Inode inode = {0};
    strncpy(inode.file_name, source_filename, MAX_FILENAME_LEN - 1);
    inode.file_size = file_size;
    inode.first_block = free_blocks[0];
    inode.file_type = (source_filename[0] == '.') ? 1 : 0;

    // Zapis danych pliku w blokach
    unsigned int current_block = inode.first_block;
    for (unsigned int i = 0; i < blocks_needed; i++) {
        unsigned int next_block = (i + 1 < blocks_needed) ? free_blocks[i + 1] : (unsigned int)-1;

        // Aktualizacja bitmapy
        block_bitmap[current_block] = true;

        // Zapis danych do aktualnego bloku
        unsigned char buffer[BLOCK_SIZE] = {0};
        fread(buffer, 1, BLOCK_SIZE, source);
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE, SEEK_SET);
        fwrite(buffer, 1, BLOCK_SIZE, disk);

        // Zapis wskaźnika na kolejny blok
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE + BLOCK_SIZE - sizeof(int), SEEK_SET);
        fwrite(&next_block, sizeof(int), 1, disk);

        current_block = next_block;
    }

    // Zapis bitmapy bloków i i-odów
    inode_bitmap[inode_index] = true;
    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fwrite(block_bitmap, sizeof(bool), metadata.num_blocks, disk);
    fwrite(inode_bitmap, sizeof(bool), MAX_FILES, disk);

    // Dodanie Inode do katalogu
    fseek(disk, sizeof(DiskMetadata) + metadata.num_blocks + MAX_FILES, SEEK_SET);
    fseek(disk, inode_index * sizeof(Inode), SEEK_CUR);
    fwrite(&inode, sizeof(Inode), 1, disk);

    // Aktualizacja metadanych
    metadata.num_files++;
    fseek(disk, 0, SEEK_SET);
    fwrite(&metadata, sizeof(DiskMetadata), 1, disk);

    // Sprzątanie
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
    fclose(source);

    printf("Plik '%s' został skopiowany na wirtualny dysk.\n", source_filename);
}

void copy_file_from_disk(const char *disk_filename, const char *output_filename) {
    // Otwórz wirtualny dysk
    FILE *disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Nie udało się otworzyć pliku dysku");
        exit(EXIT_FAILURE);
    }

    // Wczytaj metadane
    DiskMetadata metadata;
    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    // Wczytaj bitmapę i-węzłów
    bool *inode_bitmap = malloc(MAX_FILES * sizeof(bool));
    fseek(disk, sizeof(DiskMetadata) + (metadata.num_blocks * sizeof(bool)), SEEK_SET);
    fread(inode_bitmap, sizeof(bool), MAX_FILES, disk);

    // Znajdź plik w katalogu i-węzłów
    Inode *inode_catalog = malloc(MAX_FILES * sizeof(Inode));
    fseek(disk, sizeof(DiskMetadata) + (metadata.num_blocks * sizeof(bool)) + (MAX_FILES * sizeof(bool)), SEEK_SET);
    fread(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    Inode *file_inode = NULL;
    for (unsigned int i = 0; i < MAX_FILES; i++) {
        bool ity = inode_bitmap[i];
        char pierwsza_litera = inode_catalog[i].file_name[0];
        if (inode_bitmap[i] && strcmp(inode_catalog[i].file_name, output_filename) == 0) {
            file_inode = &inode_catalog[i];
            break;
        }
    }

    if (!file_inode) {
        fprintf(stderr, "Plik '%s' nie został znaleziony na wirtualnym dysku.\n", output_filename);
        free(inode_bitmap);
        free(inode_catalog);
        fclose(disk);
        return;
    }

    // Otwórz plik wyjściowy do zapisu
    FILE *output_file = fopen(output_filename, "wb");
    if (!output_file) {
        perror("Nie udało się utworzyć pliku wyjściowego");
        free(inode_bitmap);
        free(inode_catalog);
        fclose(disk);
        return;
    }

    // Kopiuj dane pliku z dysku wirtualnego
    unsigned int current_block = file_inode->first_block;
    unsigned int bytes_remaining = file_inode->file_size;

    unsigned char buffer[BLOCK_SIZE];
    while (current_block != (unsigned int)-1 && bytes_remaining > 0) {
        // Odczytaj dane z bloku
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE, SEEK_SET);
        fread(buffer, 1, BLOCK_SIZE, disk);

        // Zapisz dane do pliku wyjściowego
        unsigned int bytes_to_write = (bytes_remaining > BLOCK_SIZE) ? BLOCK_SIZE : bytes_remaining;
        fwrite(buffer, 1, bytes_to_write, output_file);

        // Odczytaj wskaźnik na następny blok
        fseek(disk, metadata.first_data_block + current_block * BLOCK_SIZE + BLOCK_SIZE - sizeof(int), SEEK_SET);
        fread(&current_block, sizeof(int), 1, disk);

        bytes_remaining -= bytes_to_write;
    }

    printf("Plik '%s' został pomyślnie skopiowany z wirtualnego dysku.\n", output_filename);

    // Sprzątanie
    free(inode_bitmap);
    free(inode_catalog);
    fclose(output_file);
    fclose(disk);
}


void display_block_bitmap(const char *disk_filename) {
    // Otwieranie pliku dysku
    FILE *disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Nie udało się otworzyć pliku dysku");
        exit(EXIT_FAILURE);
    }

    // Wczytywanie metadanych
    DiskMetadata metadata;
    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    // Wczytanie bitmapy zajętości bloków
    bool *block_bitmap = malloc(metadata.num_blocks * sizeof(bool));
    fseek(disk, sizeof(DiskMetadata), SEEK_SET);
    fread(block_bitmap, sizeof(bool), metadata.num_blocks, disk);

    // Wyświetlenie indeksów zajętych bloków
    printf("Indeksy zajętych bloków:\n");

    for (unsigned int i = 0; i < metadata.num_blocks; i++) {
        if (block_bitmap[i]) {
            printf("Blok %u jest zajęty\n", i);
        }
    }
    printf("Pozostałe bloki są wolne.\n");
}

void list_files_on_disk(const char *disk_filename, bool show_hidden) {
    FILE *disk = fopen(disk_filename, "rb");
    if (!disk) {
        perror("Nie udało się otworzyć pliku dysku");
        exit(EXIT_FAILURE);
    }

    // Wczytanie metadanych
    DiskMetadata metadata;
    fread(&metadata, sizeof(DiskMetadata), 1, disk);

    // Wczytanie bitmapy i katalogu i-węzłów
    fseek(disk, sizeof(DiskMetadata) + metadata.num_blocks * sizeof(bool) + MAX_FILES * sizeof(bool), SEEK_SET);

    // Wczytanie katalogu i-węzłów
    Inode *inode_catalog = malloc(MAX_FILES * sizeof(Inode));
    if (!inode_catalog) {
        perror("Nie udało się zaalokować pamięci dla katalogu i-węzłów");
        fclose(disk);
        exit(EXIT_FAILURE);
    }
    fread(inode_catalog, sizeof(Inode), MAX_FILES, disk);

    printf("%-20s %-10s %-10s\n", "Nazwa pliku", "Rozmiar", "Pierwszy blok");
    printf("---------------------------------------------\n");

    for (unsigned int i = 0; i < MAX_FILES; i++) {
        if (inode_catalog[i].file_name[0] != '\0') { // Sprawdzenie, czy plik istnieje
            if (inode_catalog[i].file_type == 1 && !show_hidden) {
                continue;
            }
            printf("%-20s %-10u %-10u\n", 
                inode_catalog[i].file_name, 
                inode_catalog[i].file_size, 
                inode_catalog[i].first_block);
        }
    }

    free(inode_catalog);
    fclose(disk);
}


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
        printf("0. Zakończ program\n");
        printf("Twój wybór: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                printf("Podaj nazwę pliku do skopiowania na dysk: ");
                scanf("%s", filename);
                copy_file_to_disk(disk_filename, filename);
                break;

            case 2:
                printf("Podaj nazwę pliku do skopiowania z dysku: ");
                scanf("%s", filename);
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

            default:
                printf("Nieprawidłowy wybór. Spróbuj ponownie.\n");
        }
    }
    return 0;
}