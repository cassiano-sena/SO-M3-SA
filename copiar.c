#include "fat16.h"   // Supondo que o FAT16 foi corretamente definido.
#include "commands.h"// Para os protótipos de funções.

int cp_from_fat16(const char *filename, const char *dest) {
    FILE *image = fopen("fat16.img", "rb"); // Abre a imagem FAT16.
    if (!image) {
        perror("Error opening FAT16 image");
        return -1;
    }

    FILE *output = fopen(dest, "wb"); // Abre o arquivo de destino no sistema de arquivos do host.
    if (!output) {
        perror("Error opening output file");
        fclose(image);
        return -1;
    }

    struct DirEntry *entry = find_directory_entry(image, filename);
    if (!entry) {
        printf("File not found: %s\n", filename);
        fclose(image);
        fclose(output);
        return -1;
    }

    int cluster = entry->firstCluster; // Obtém o primeiro cluster do arquivo.
    while (cluster < 0xFFF8) { // 0xFFF8 indica o final do arquivo na FAT16.
        void *clusterData = read_cluster(image, cluster);
        fwrite(clusterData, 1, CLUSTER_SIZE, output);
        cluster = get_next_cluster(image, cluster); // Obtém o próximo cluster a partir da tabela FAT.
        free(clusterData);
    }

    fclose(image);
    fclose(output);
    return 0;
}
