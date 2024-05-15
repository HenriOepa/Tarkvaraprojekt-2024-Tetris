ADDED 15/05/2024

Required libraries:
  GTK4 (and its dependencies),
  SDL2,
  SQLite3,
  pthread (on Windows)

For compilation, Git Bash was used. Open it in the folder where the files are located and use the following command:

gcc [file name].c -o [output name].exe $(pkg-config --cflags --libs GTK4 SDL2 sqlite3)

An exe file should be created in the folder and the program can be run
