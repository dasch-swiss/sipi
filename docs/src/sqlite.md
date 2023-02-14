# Using SQLite in SIPI

Sipi supports [SQLite](https://www.sqlite.org/) 3 databases, which can
be accessed from Lua scripts. You should use
[pcall](https://www.lua.org/pil/8.4.html) to handle errors that may be
returned by SQLite.

## Opening an SQLite Database

    db = sqlite(path_to_db, access)

This creates a new opaque database object. The parameters are:

- `path_to_db`: path to the sqlite3 database file.
- `access`: Method of opening the database. Allowed are
    - `'RO'`: readonly access. The file must exist and the SPIP server must have read access to it.
    - `'RW'`: read and write access. The file must exist and the SPIP server must have read/write access to it.
    - `'CRW'`: If the database file does not exist, it will be created and opened with read/write access.


To destroy the database object and free all resources, you can do this:

    db = ~db

However, Lua's garbage collection will destroy the database object and
free all resources when they are no longer used.

### Preparing a Query
The SIPI sqlite interface supports direct queries as well as prepared statements. A direct query is constructed as
follows:

    qry = db << 'SELECT * FROM image'

Or, if you want to use a prepared query statement:

    qry = db << 'INSERT INTO image (id, description) VALUES (?,?)'

The result of the `<<` operator (`qry`) will then be a query object containing a prepared query. If the
query object is not needed anymore, it may be destroyed:

    qry = ~qry

Query objects should be destroyed explicitly if not needed any longer.

### Executing a Query
Excuting (calling) a query objects gets the next row of data. If there are no more rows, `nil` is returned. The row is
returned as array of values.

    row = qry()
    while (row) do
        print(row[0], ' -> ', row[1])
        row = qry()
    end

Or with a prepared statement:

    row = qry('SGV_1960_00315', 'This is an image of a steam engine...')

The second way is used for prepared queries that contain parameters.
