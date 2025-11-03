## membuat notepad desktop

donwload SQLite for c: `https://www.sqlite.org/download.html`

klik ini: `sqlite-amalgamation-XXXXXXX.zip`
kemudian setelah terdownload extrack ke dalam project yg kita buat,

berikut gambaran strucktur folder nya nanti:

```
D:\C++\notepad-desktop\
├─ main.cpp
├─ sqlite3.c
├─ sqlite3.h
```

untuk jalaninnya:
`gcc -c sqlite3.c -o sqlite3.o`
`g++ main.cpp sqlite3.o -o notepad_sqlite.exe -mwindows`

kemudian jalanin ini:
`.\notepad_sqlite.exe`