#include "fat16.h"
#include "commands.h"

int rm_from_fat16(const char *filename) {
    FILE *image = fopen("fat16.img", "rb+"); // Abre a imagem FAT16.
    if (!image) {
        perror("Error opening FAT16 image");
        return -1;
    }

    struct DirEntry *entry = find_directory_entry(image, filename);
    if (!entry) {
        printf("File not found: %s\n", filename);
        fclose(image);
        return -1;
    }

    int cluster = entry->firstCluster;
    while (cluster < 0xFFF8) {
        int nextCluster = get_next_cluster(image, cluster);
        free_cluster(image, cluster);
        cluster = nextCluster;
    }

    if (delete_directory_entry(image, entry) < 0) {
        printf("Error deleting directory entry for %s\n", filename);
        fclose(image);
        return -1;
    }

    fclose(image);
    return 0;
}
