#include "fat16.h"
#include "commands.h"

int mv_to_fat16(const char *source, const char *dest) {
    FILE *image = fopen("fat16.img", "rb+"); // Abre a imagem FAT16.
    if (!image) {
        perror("Error opening FAT16 image");
        return -1;
    }

    FILE *input = fopen(source, "rb"); // Abre o arquivo de origem no sistema de arquivos do host.
    if (!input) {
        perror("Error opening input file");
        fclose(image);
        return -1;
    }

    // Alocar clusters para o novo arquivo.
    int firstCluster = allocate_clusters(image, input);
    if (firstCluster < 0) {
        printf("Error allocating clusters for %s\n", dest);
        fclose(image);
        fclose(input);
        return -1;
    }

    struct DirEntry newEntry;
    memset(&newEntry, 0, sizeof(struct DirEntry));
    strncpy(newEntry.filename, dest, 11);
    newEntry.firstCluster = firstCluster;
    newEntry.size = get_file_size(input);

    if (write_directory_entry(image, &newEntry) < 0) {
        printf("Error writing directory entry for %s\n", dest);
        fclose(image);
        fclose(input);
        return -1;
    }

    fclose(image);
    fclose(input);
    return 0;
}
