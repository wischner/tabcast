# apostle/tests/

Automated tests. Test data files go in `tests/data/`.

Nothing here yet. Each `.cpp` in this directory becomes its own test
binary, linked against every object of the program except
`src/main.o`, so a test brings its own `main()`. Run them all with
`make test`.
