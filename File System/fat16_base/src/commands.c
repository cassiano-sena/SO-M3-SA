#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "commands.h"
#include "fat16.h"
#include "support.h"

off_t fsize(const char *filename){
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

struct fat_dir find(struct fat_dir *dirs, char *filename, struct fat_bpb *bpb){
    struct fat_dir curdir;
    int dirs_len = sizeof(struct fat_dir) * bpb->possible_rentries;
    int i;

    for (i=0; i < dirs_len; i++){
        if (strcmp((char *) dirs[i].name, filename) == 0){
            curdir = dirs[i];
            break;
        }
    }
    return curdir;
}

struct fat_dir *ls(FILE *fp, struct fat_bpb *bpb){
    int i;
    struct fat_dir *dirs = malloc(sizeof (struct fat_dir) * bpb->possible_rentries);

    for (i=0; i < bpb->possible_rentries; i++){
        uint32_t offset = bpb_froot_addr(bpb) + i * 32;
        read_bytes(fp, offset, &dirs[i], sizeof(dirs[i]));
    }
    return dirs;
}

int write_dir(FILE *fp, char *fname, struct fat_dir *dir){
    char* name = padding(fname);
    strcpy((char *) dir->name, (char *) name);
    if (fwrite(dir, 1, sizeof(struct fat_dir), fp) <= 0)
        return -1;
    return 0;
}

int write_data(FILE *fp, char *fname, struct fat_dir *dir, struct fat_bpb *bpb){

    FILE *localf = fopen(fname, "r");
    int c;

    while ((c = fgetc(localf)) != EOF){
        if (fputc(c, fp) != c)
            return -1;
    }
    return 0;
}

int wipe(FILE *fp, struct fat_dir *dir, struct fat_bpb *bpb){
    int start_offset = bpb_froot_addr(bpb) + (bpb->bytes_p_sect * \
            dir->starting_cluster);
    int limit_offset = start_offset + dir->file_size;

    while (start_offset <= limit_offset){
        fseek(fp, ++start_offset, SEEK_SET);
        if(fputc(0x0, fp) != 0x0)
            return 01;
    }
    return 0;
}

void rm(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Encontra a entrada de diretório do arquivo na FAT16
    struct fat_dir *dirs = ls(fp, bpb);
    struct fat_dir dir = find(dirs, filename, bpb);
    free(dirs);

    if (dir.name[0] == 0x00 || dir.name[0] == 0xE5) {
        fprintf(stderr, "Arquivo não encontrado na imagem FAT16.\n");
        return;
    }

    // Marca os clusters usados pelo arquivo como livres na FAT
    int cluster = dir.starting_cluster;
    while (cluster >= 0x0002 && cluster <= 0xFFEF) {
        int next_cluster = get_next_cluster(fp, cluster, bpb);
        free_cluster(fp, cluster);
        cluster = next_cluster;
    }

    // Remove a entrada do diretório
    dir.name[0] = 0xE5; // Marca a entrada como excluída
    if (write_dir(fp, filename, &dir) != 0) {
        fprintf(stderr, "Erro ao remover a entrada do diretório.\n");
    } else {
        printf("Arquivo removido com sucesso da imagem FAT16.\n");
    }
}


void mv(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Abre o arquivo de origem no sistema de arquivos do host
    FILE *src = fopen(filename, "rb");
    if (!src) {
        fprintf(stderr, "Erro ao abrir o arquivo de origem.\n");
        return;
    }

    // Obtém o tamanho do arquivo de origem
    off_t filesize = fsize(filename);
    if (filesize < 0) {
        fprintf(stderr, "Erro ao obter o tamanho do arquivo.\n");
        fclose(src);
        return;
    }

    // Calcula o número de clusters necessários
    int bytes_per_cluster = bpb->bytes_p_sect * bpb->sect_p_clust;
    int clusters_needed = (filesize + bytes_per_cluster - 1) / bytes_per_cluster;

    // Aloca clusters livres na FAT
    int starting_cluster = allocate_clusters(fp, clusters_needed, bpb);
    if (starting_cluster < 0) {
        fprintf(stderr, "Erro ao alocar clusters na FAT.\n");
        fclose(src);
        return;
    }

    // Cria a entrada de diretório para o arquivo na FAT16
    struct fat_dir new_entry;
    memset(&new_entry, 0, sizeof(struct fat_dir));
    strncpy((char *)new_entry.name, padding(filename), 11);
    new_entry.starting_cluster = starting_cluster;
    new_entry.file_size = filesize;

    // Escreve os dados do arquivo de origem nos clusters alocados na FAT16
    int cluster = starting_cluster;
    char buffer[bytes_per_cluster];
    while (cluster >= 0x0002 && cluster <= 0xFFEF) {
        uint32_t cluster_offset = bpb_cluster_addr(bpb, cluster);
        fseek(fp, cluster_offset, SEEK_SET);
        int bytes = fread(buffer, 1, bytes_per_cluster, src);
        fwrite(buffer, 1, bytes, fp);
        if (bytes < bytes_per_cluster) break;
        cluster = get_next_cluster(fp, cluster, bpb);
    }

    fclose(src);
    if (write_dir(fp, filename, &new_entry) != 0) {
        fprintf(stderr, "Erro ao escrever a entrada do diretório.\n");
    } else {
        printf("Arquivo movido com sucesso para a imagem FAT16.\n");
    }
}


void cp(FILE *fp, char *filename, struct fat_bpb *bpb) {
    // Encontra a entrada de diretório do arquivo na FAT16
    struct fat_dir *dirs = ls(fp, bpb);
    struct fat_dir dir = find(dirs, filename, bpb);
    free(dirs);

    if (dir.name[0] == 0x00 || dir.name[0] == 0xE5) {
        fprintf(stderr, "Arquivo não encontrado na imagem FAT16.\n");
        return;
    }

    // Abre o arquivo de destino no sistema de arquivos do host
    FILE *dest = fopen(filename, "wb");
    if (!dest) {
        fprintf(stderr, "Erro ao abrir o arquivo de destino.\n");
        return;
    }

    // Lê e copia os dados dos clusters do arquivo
    int cluster = dir.starting_cluster;
    int bytes_to_read = dir.file_size;
    int bytes_per_cluster = bpb->bytes_p_sect * bpb->sect_p_clust;
    char buffer[bytes_per_cluster];

    while (cluster >= 0x0002 && cluster <= 0xFFEF && bytes_to_read > 0) {
        uint32_t cluster_offset = bpb_cluster_addr(bpb, cluster);
        fseek(fp, cluster_offset, SEEK_SET);
        int bytes = fread(buffer, 1, bytes_per_cluster, fp);
        fwrite(buffer, 1, bytes, dest);
        bytes_to_read -= bytes;
        cluster = get_next_cluster(fp, cluster, bpb);
    }

    fclose(dest);
    if (bytes_to_read != 0) {
        fprintf(stderr, "Erro ao copiar o arquivo completo.\n");
    } else {
        printf("Arquivo copiado com sucesso para o sistema de arquivos do host.\n");
    }
}


