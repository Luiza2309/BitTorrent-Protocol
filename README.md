# tema3
- protocolul este implementat cum a fost descris in cerinta

- peer
    - citirea din fisier
    - anuntarea trackerului despre fisierele pe care le detine
    - pornirea threadurilor de download/upload

- download_thread_function
    - cere trackerului o lista cu peers pentru fisierele pe care vrea sa le descarce
    - pentru schimbarea peer-ului iterez prin vectorul de peers la fiecare segment folosind variabila which_peer
    - trimit cerere pentru hash-ul dorit si il primesc
    - daca exista deja fisierul in fisierele detinute, adaug hashul, daca nu, creez fisierul
    - la fiecare 10 pasi actualizez trackerul cu hash-urile pe care le-am descarcat
    - cand termin de descarcat un fisier, ii trimit trackerului restul de hash-uri pe care le-am mai descarcat
    - se scrie in fisier
    - cand termin de trimis toate fisierele, trimit un anunt

- upload
    - daca primeste o cerere de hash si e valid (!= -1), caut fisierul dorit in lista de fisiere si trimit hash-ul
    - daca numarul hash-ului este -1, acesta este un mesaj de la tracker ca trebuie inchis 

- tracker
    - in functie de tag-ul pe care il are mesajul se intampla actiuni diferite:
        - 100 => a primit o cerere de la un client sa ii trimita peers pentru un anumit fisier (functia peers_request_messages)
        - 200 => a primit o actualizare de la un client (functia update)
        - 300 => un client a terminat de descarcat un fisier
        - 400 => un client a terminat de descarcat toate fisierele
    - dupa ce toti clientii au trimis mesaj ca au terminat de descarcat toate fisierele, trackerul trimite un mesaj de oprire

- init
    - toti clientii trimit toate fisierele pe care le detin
    - primesc numele si hashurile pe care le salvez intr-o structura File
    - in structura PeerInfo salvez rankul clientului si fisierul pe care il detine
    - swarm-ul este de forma unordered_map<string, vector<PeerInfo>> pentru a putea accesa usor informatia de la un anumit
    fisier dupa numele sau, iar vectorul salveaza peers si fisierul in sine pentru a stii cate hashuri detin din el


- peers_request_messages
    - primeste numele fisierului pe care clientul doreste sa il descarce si ii trimite peers si segmentele pe care le au

- update
    - cauta in swarm rankul clientului care vrea sa-si actualizeze informatia si daca nu exista in swarm la fisierul
    respectiv il adauga, dupa care primeste hashurile noi si actualizeaza intrarea in swarm
