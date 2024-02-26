#include <mpi.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <unordered_map>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

struct File {
    string filename;
    int number_of_hashes;
    vector<string> hashes;
};

struct PeerInfo {
    int rank;
    File file;
};

struct DownloadingArguments {
    int rank;
    int files_wanted;
    vector<string> files_wanted_vec;
};

int files_owned;
vector<File> files_owned_vec;

unordered_map<string, vector<PeerInfo>> which_peers_are_for_my_file(int files_wanted, vector<string> files_wanted_vec) {
    unordered_map<string, vector<PeerInfo>> peers_for_files;
    
    int ack;
    MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD);

    MPI_Send(&files_wanted, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD);
    for (int i = 0; i < files_wanted; i++) {
        // trimit ce fisiere vreau
        int file_size = files_wanted_vec[i].size() + 1;
        MPI_Send(&file_size, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD);
        MPI_Send(files_wanted_vec[i].c_str(), files_wanted_vec[i].size() + 1, MPI_CHAR, TRACKER_RANK, 100, MPI_COMM_WORLD);
        
        // primesc numarul de peers
        int number_of_peers;
        MPI_Recv(&number_of_peers, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        vector<PeerInfo> peer_info_vec(number_of_peers);

        // primesc peers unul cate unul
        for (int j = 0; j < number_of_peers; j++) {
            int peer_rank, number_of_segments;
            vector<string> segments;
            MPI_Recv(&peer_rank, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&number_of_segments, 1, MPI_INT, TRACKER_RANK, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // primesc segmentele pe care le are
            for (int k = 0; k < number_of_segments; k++) {
                char* segment = new char[HASH_SIZE + 1];
                MPI_Recv(segment, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                string hash(segment);
                segments.push_back(hash);
            }

            File f;
            f.filename = files_wanted_vec[i];
            f.number_of_hashes = number_of_segments;
            f.hashes = segments;

            PeerInfo p;
            p.file = f;
            p.rank = peer_rank;

            peer_info_vec[j] = p;
        }

        peers_for_files.insert({files_wanted_vec[i], peer_info_vec});
    }

    return peers_for_files;
}

void* download_thread_func(void *arg) {
    DownloadingArguments* a = (DownloadingArguments*) arg;
    int rank = a->rank;
    int files_wanted = a->files_wanted;
    vector<string> files_wanted_vec = a->files_wanted_vec;
    // DESCARCAREA

    // PASUL 1: II CERE TRACKER-ULUI O LISTA DE PEERS PENTRU FISIERELE PE CARE LE DORESTE
    unordered_map<string, vector<PeerInfo>> peers_for_files;
    peers_for_files = which_peers_are_for_my_file(files_wanted, files_wanted_vec);

    // PASUL 2: VAD CE SEGMENTE II LIPSESC
    for (int file_downloading = 0; file_downloading < files_wanted; file_downloading++) {
        string filename_wanted = files_wanted_vec[file_downloading];
        int filename_size = filename_wanted.size();
        vector<PeerInfo> peer_info_vec = peers_for_files.at(files_wanted_vec[file_downloading]);

        int current_hash = peer_info_vec[0].file.number_of_hashes - 1;
        // PASUL 3a: alege un peer pe care nu l-a mai ales
        unsigned int which_peer = 0;
        vector<string> every_10_steps;
        while (current_hash >= 0) {
            int peer_rank = peer_info_vec[which_peer].rank;
            File file = peer_info_vec[which_peer].file;

            // PASUL 3b: trimite o cerere pentru segment
            MPI_Send(&current_hash, 1, MPI_INT, peer_rank, 1, MPI_COMM_WORLD);

            MPI_Send(&filename_size, 1, MPI_INT, peer_rank, 2, MPI_COMM_WORLD);
            MPI_Send(filename_wanted.c_str(), filename_size + 1, MPI_CHAR, peer_rank, 3, MPI_COMM_WORLD);

            // PASUL 3c: primeste segmentul
            char* chunk = new char[HASH_SIZE + 1];
            MPI_Recv(chunk, HASH_SIZE + 1, MPI_CHAR, peer_rank, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            string hash(chunk);

            // PASUL 3d: marcheaza segmentul ca primit
            bool found = false;
            for (File& f : files_owned_vec) {
                // daca exista deja fisierul si am mai primit un hash
                if (f.filename == filename_wanted) {
                    found = true;
                    f.number_of_hashes++;
                    f.hashes[current_hash] = hash;
                    break;
                }
            }

            if (found == false) {
                // daca nu exista fisierul si e primul hash primit
                File* new_file = new File;
                new_file->filename = filename_wanted;
                new_file->number_of_hashes = 1;
                new_file->hashes.resize(current_hash + 1);
                new_file->hashes[current_hash] = hash;

                files_owned_vec.push_back(*new_file);
                files_owned++;
            }

            current_hash --;
            
            which_peer++;
            if (which_peer >= peer_info_vec.size()) which_peer = 0;

            every_10_steps.push_back(hash);

            // ACTUALIZARE
            if (every_10_steps.size() == 10) {
                int ack;
                MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, 200, MPI_COMM_WORLD);

                // din ce fisier am descarcat
                MPI_Send(&filename_size, 1, MPI_INT, TRACKER_RANK, 200, MPI_COMM_WORLD);
                MPI_Send(filename_wanted.c_str(), filename_size + 1, MPI_CHAR, TRACKER_RANK, 200, MPI_COMM_WORLD);

                // cat am descarcat
                int how_many = 10;
                MPI_Send(&how_many, 1, MPI_INT, TRACKER_RANK, 200, MPI_COMM_WORLD);

                // actualizeaza trackerul despre ce segmente detine
                for (unsigned int update_for_tracker = 0; update_for_tracker < 10; update_for_tracker++) {
                    MPI_Send(every_10_steps[update_for_tracker].c_str(), HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 200, MPI_COMM_WORLD);
                }

                // reactualizeaza lista de peers
                vector<string> send;
                send.push_back(filename_wanted);
                peer_info_vec = which_peers_are_for_my_file(1, send).at(filename_wanted);

                every_10_steps.clear();
            }
        }

        // S-A TERMINAT DE DESCARCAT UN FISIER
        int ack;
        MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, 300, MPI_COMM_WORLD);

        // fisierul care s-a descarcat
        MPI_Send(&filename_size, 1, MPI_INT, TRACKER_RANK, 300, MPI_COMM_WORLD);
        MPI_Send(filename_wanted.c_str(), filename_size + 1, MPI_CHAR, TRACKER_RANK, 300, MPI_COMM_WORLD);

        // ultimele hash-uri pe care nu le-am trimis inca in update
        int end_hashes = every_10_steps.size();
        MPI_Send(&end_hashes, 1, MPI_INT, TRACKER_RANK, 300, MPI_COMM_WORLD);

        for (int i = 0; i < end_hashes; i++) {
            MPI_Send(every_10_steps[i].c_str(), HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 300, MPI_COMM_WORLD);
        }

        every_10_steps.clear();

        // SCRIEREA IN FISIER
        char filename[20];
        snprintf(filename, sizeof(filename), "client%d_%s", rank, filename_wanted.c_str());
        ofstream fout(filename);
        if (!fout.is_open()) {
            cout << "Nu s-a putut deschide fisierul.\n";
            return nullptr;
        }

        for (int i = 0; i < files_owned_vec[files_owned - 1].number_of_hashes; i++) {
            fout << files_owned_vec[files_owned - 1].hashes[i] << endl;
        }

        fout.close();
    }

    // A TERMINAT DE DESCARCAT TOATE FISIERELE
    int ack;
    MPI_Send(&ack, 1, MPI_INT, TRACKER_RANK, 400, MPI_COMM_WORLD);

    return nullptr;
}

void* upload_thread_func(void *arg) {
    int rank = *(int*) arg;

    while (true) {
        int required_hash;
        MPI_Status status;

        // un client cere un hash
        MPI_Recv(&required_hash, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        
        // trackerul a trimis mesaj de oprire
        if (required_hash == -1) break;
        
        // fisierul din care e hash-ul
        int filename_size;
        MPI_Recv(&filename_size, 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        char* required_file = new char[filename_size + 1];
        MPI_Recv(required_file, filename_size + 1, MPI_CHAR, status.MPI_SOURCE, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // caut fisierul si ii trimit hash-ul
        for (int i = 0; i < files_owned; i ++) {
            if (strcmp(files_owned_vec[i].filename.c_str(), required_file) == 0) {
                MPI_Send(files_owned_vec[i].hashes[required_hash].c_str(), HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 4, MPI_COMM_WORLD);
                break;
            }
        }
    }

    return nullptr;
}

unordered_map<string, vector<PeerInfo>> init(int numtasks) {
    unordered_map<string, vector<PeerInfo>> swarm;

    // initial toate fisierele imi trimit fisierele pe care le detin
    for (int task = 1; task < numtasks; task++) {
        int files_owned;
        MPI_Recv(&files_owned, 1, MPI_INT, task, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int i = 0; i < files_owned; i++) {
            // primeste numele fisierului
            int filename_size;
            MPI_Recv(&filename_size, 1, MPI_INT, task, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            char filename[filename_size];
            MPI_Recv(filename, filename_size, MPI_CHAR, task, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            File* f = new File;
            f->filename = filename;

            // primeste fiecare hash din fisierul respectiv
            int number_of_hashes;
            MPI_Recv(&number_of_hashes, 1, MPI_INT, task, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            f->hashes.resize(number_of_hashes);
            f->number_of_hashes = number_of_hashes;
            for (int j = 0; j < number_of_hashes; j++) {
                char hash[HASH_SIZE + 1];
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, task, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                f->hashes[j] = hash;
            }

            PeerInfo p;
            p.file = *f;
            p.rank = task;

            // PASUL 2: TRECE CLIENTUL IN SWARM-UL FISIERULUI
            if (swarm.find(f->filename) != swarm.end()) {
                // fisierul deja exista; baga clientul in swarm
                swarm.at(f->filename).push_back(p);
            } else {
                // fisierul nu exista in swarm; se creeaza fisierul
                vector<PeerInfo> aux;
                aux.push_back(p);
                swarm.insert({f->filename, aux});
            }
        }
    }

    return swarm;
}

void peers_request_messages(int task, int tag, unordered_map<string, vector<PeerInfo>> swarm) {
    // primeste cate fisiere vrea sa descarce clientul curent
    int files_wanted;
    MPI_Recv(&files_wanted, 1, MPI_INT, task, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    vector<string> files_wanted_vec(files_wanted);

    for (int i = 0; i < files_wanted; i++) {
        // primeste ce fisiere vrea fiecare client sa descarce
        int file_size;
        MPI_Recv(&file_size, 1, MPI_INT, task, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        char aux[file_size];
        MPI_Recv(aux, file_size, MPI_CHAR, task, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        files_wanted_vec[i] = aux;

        // trimite numarul de peers
        vector<PeerInfo> peers = swarm.at(files_wanted_vec[i]);
        int number_of_peers = peers.size();
        MPI_Send(&number_of_peers, 1, MPI_INT, task, tag, MPI_COMM_WORLD);

        // trimite peers unul cate unul
        for (int j = 0; j < number_of_peers; j++) {
            MPI_Send(&peers[j].rank, 1, MPI_INT, task, tag, MPI_COMM_WORLD);
            MPI_Send(&peers[j].file.number_of_hashes, 1, MPI_INT, task, tag, MPI_COMM_WORLD);

            // trimite segmentele pe care le are
            for (int k = 0; k < peers[j].file.number_of_hashes; k++) {
                MPI_Send(peers[j].file.hashes[k].c_str(), HASH_SIZE + 1, MPI_CHAR, task, tag, MPI_COMM_WORLD);
            }
        }
    }
}

void update(unordered_map<string, vector<PeerInfo>>& swarm, int source, int tag) {
    // primeste numele fisierului
    int filename_size;
    MPI_Recv(&filename_size, 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    char* filename_wanted = new char[filename_size + 1];
    MPI_Recv(filename_wanted, filename_size + 1, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    int how_many;
    MPI_Recv(&how_many, 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    vector<PeerInfo> peers_aux = swarm.at(filename_wanted);
    // cauta rankul clientului in swarm pentru a actualiza hash-urile detinute
    unsigned int find;
    for (find = 0; find < peers_aux.size(); find++) {
        if (peers_aux[find].rank == source) break;
    }

    if (find == peers_aux.size()) {
        // nu exista clientul; trebuie creat
        File f;
        f.filename = filename_wanted;
        f.number_of_hashes = 0;

        PeerInfo p;
        p.rank = source;
        p.file = f;

        peers_aux.push_back(p);
    }

    // primeste hashurile noi si actualizeaza
    for (int update = 0; update < how_many; update++) {
        char* hash_aux = new char[HASH_SIZE + 1];
        MPI_Recv(hash_aux, HASH_SIZE + 1, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        string h(hash_aux);

        peers_aux[find].file.hashes.push_back(h);
        peers_aux[find].file.number_of_hashes++;
    }
}

void tracker(int numtasks, int rank) {
    int clients = numtasks;
    // INITIALIZAREA

    // PASUL 1: ASTEAPTA MESAJELE DE LA CLIENTI CU FISIERELE DETINUTE
    unordered_map<string, vector<PeerInfo>> swarm;
    swarm = init(numtasks);

    // PASUL 3: TRIMITE ACK CA PROCESELE SA INCEAPA DESCARCAREA
    int ack = 1;
    MPI_Bcast(&ack, 1, MPI_INT, TRACKER_RANK, MPI_COMM_WORLD);

    // PRIMIRE MESAJE DE LA CLIENTI
    while (true) {
        int ack;
        MPI_Status status;
        MPI_Recv(&ack, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == 100) {
            // daca primeste o cerere de la un client pentru peers
            peers_request_messages(status.MPI_SOURCE, status.MPI_TAG, swarm); 
        } else if (status.MPI_TAG == 200) {
            // daca primeste o actualizare
            update(swarm, status.MPI_SOURCE, status.MPI_TAG);
        } else if (status.MPI_TAG == 300) {
            // daca primeste ca un client a terminat de descarcat un fisier
            update(swarm, status.MPI_SOURCE, status.MPI_TAG);
        } else if (status.MPI_TAG == 400) {
            // daca primeste ca un client a terminat de descarcat toate fisierele
            numtasks--;
        }

        // daca primeste ca toti clientii au terminat de descarcat
        if (numtasks == 1) break;
    } 

    // trimite mesaje catre clienti sa le spuna sa se opreasca
    int stop = -1;
    for (int i = 1; i < clients; i++) {
        MPI_Send(&stop, 1, MPI_INT, i, 1, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    // INITIALIZAREA

    // PASUL 1: CITESTE FISIERUL DE INTRARE
    // deschide fisierul
    char filename[MAX_FILENAME];
    snprintf(filename, sizeof(filename), "in%d.txt", rank);
    ifstream fin(filename);
    if (!fin.is_open()) {
        cout << "Nu s-a putut deschide fisierul.\n";
        return;
    }

    // citeste si salveaza fisierele
    fin >> files_owned;
    files_owned_vec.resize(files_owned);

    for (int i = 0; i < files_owned; i++) {
        File f;
        fin >> f.filename >> f.number_of_hashes;

        f.hashes.resize(f.number_of_hashes);
        for (int j = 0; j < f.number_of_hashes; j++) {
            fin >> f.hashes[j];
        }

        files_owned_vec[i] = f;
    }

    int files_wanted;
    fin >> files_wanted;
    vector<string> files_wanted_vec(files_wanted);

    for (int i = 0; i < files_wanted; i++) {
        fin >> files_wanted_vec[i];
    }

    fin.close();

    MPI_Send(&files_owned, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

    // PASUL 2: II SPUNE TRACKER-ULUI CE FISIERE ARE
    for (int i = 0; i < files_owned; i++) {
        // trimite numele fisierului
        int filename_size = files_owned_vec[i].filename.size() + 1;
        MPI_Send(&filename_size, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(files_owned_vec[i].filename.c_str(), files_owned_vec[i].filename.size() + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // trimite fiecare hash din fisierul respectiv
        int no_hashes = files_owned_vec[i].number_of_hashes;
        MPI_Send(&no_hashes, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        for (int j = 0; j < files_owned_vec[i].number_of_hashes; j++) {
            MPI_Send(files_owned_vec[i].hashes[j].c_str(), HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        }
    }

    // PASUL 3: ASTEAPTA CONFIRMARE DE LA TRACKER SA INCEAPA DESCARCAREA
    // tracker-ul face broadcast
    int ack;
    MPI_Bcast(&ack, 1, MPI_INT, TRACKER_RANK, MPI_COMM_WORLD);

    DownloadingArguments* a = new DownloadingArguments;
    a->rank = rank;
    a->files_wanted = files_wanted;
    a->files_wanted_vec = files_wanted_vec;
    r = pthread_create(&download_thread, nullptr, download_thread_func, (void *) a);
    if (r) {
        cout << "Eroare la crearea thread-ului de download\n";
        exit(-1);
    }

    r = pthread_create(&upload_thread, nullptr, upload_thread_func, (void *) &rank);
    if (r) {
        cout << "Eroare la crearea thread-ului de upload\n";
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        cout << "Eroare la asteptarea thread-ului de download\n";
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        cout << "Eroare la asteptarea thread-ului de upload\n";
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        cout << "MPI nu suporta multi-threading\n";
        exit(-1);
    }

    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
