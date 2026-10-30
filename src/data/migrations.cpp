#include "data/migrations.h"

#include <iterator>
#include <string>

namespace ninkasi::data {

namespace {

struct Migration {
    int version;
    const char* sql;
};

// Timestamps are stored as ISO-8601 UTC text, e.g. 2026-10-27T14:03:00Z.
constexpr Migration kMigrations[] = {
    {1, R"sql(
        -- Users exist from day one so every log row can record who did it.
        -- Until login arrives there is a single local user.
        CREATE TABLE users (
            id            INTEGER PRIMARY KEY,
            username      TEXT NOT NULL UNIQUE,
            display_name  TEXT NOT NULL,
            password_hash TEXT,                       -- NULL until login exists
            role          TEXT NOT NULL DEFAULT 'admin',
            created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        INSERT INTO users (username, display_name) VALUES ('local', 'Local Brewer');

        -- Brewery-wide preferences (theme, units, ...) as key/value text.
        CREATE TABLE settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE audit_log (
            id        INTEGER PRIMARY KEY,
            at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
            user_id   INTEGER NOT NULL REFERENCES users(id),
            action    TEXT NOT NULL,
            entity    TEXT,
            entity_id INTEGER,
            details   TEXT
        );
    )sql"},
    {2, R"sql(
        -- Equipment on the brewery floor. Positions and sizes are in floor
        -- units: one unit is one point at 100 % zoom.
        CREATE TABLE elements (
            id         INTEGER PRIMARY KEY,
            type       TEXT NOT NULL,
            name       TEXT NOT NULL,
            x          REAL NOT NULL,
            y          REAL NOT NULL,
            width      REAL NOT NULL,
            height     REAL NOT NULL,
            capacity_l REAL,                              -- vessels only
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- Units an element shows instead of the brewery-wide choice.
        CREATE TABLE element_unit_overrides (
            element_id INTEGER NOT NULL REFERENCES elements(id) ON DELETE CASCADE,
            dimension  TEXT NOT NULL,
            unit       TEXT NOT NULL,
            PRIMARY KEY (element_id, dimension)
        );
    )sql"},
    {3, R"sql(
        -- Recipes. The body is the whole recipe as JSON (data/recipe_json.cpp):
        -- it is always read and saved as one document, and batches will keep
        -- their own copy of it.
        CREATE TABLE recipes (
            id         INTEGER PRIMARY KEY,
            name       TEXT NOT NULL,
            style      TEXT NOT NULL DEFAULT '',
            body       TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
            updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- The ingredient library: fermentables, hops and yeasts.
        CREATE TABLE ingredients (
            id         INTEGER PRIMARY KEY,
            kind       TEXT NOT NULL,                     -- 'fermentable', 'hop' or 'yeast'
            name       TEXT NOT NULL,
            body       TEXT NOT NULL,                     -- the ingredient as JSON
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX ingredients_by_kind ON ingredients (kind, name);
    )sql"},
    {4, R"sql(
        -- Batches of beer, from brew day until they are packaged or dumped.
        -- `recipe` is the recipe as brewed (JSON), kept even if the recipe
        -- is later changed or deleted.
        CREATE TABLE batches (
            id          INTEGER PRIMARY KEY,
            code        TEXT NOT NULL UNIQUE,          -- the brewery's batch number
            name        TEXT NOT NULL,                 -- the beer, for lists
            style       TEXT NOT NULL DEFAULT '',
            recipe_id   INTEGER REFERENCES recipes(id) ON DELETE SET NULL,
            recipe      TEXT NOT NULL,
            status      TEXT NOT NULL,                 -- 'brewing', 'fermenting', 'conditioning', 'packaged', 'dumped'
            created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
            finished_at TEXT
        );

        -- Brewhouse turns. Most batches have one; filling a large fermenter
        -- can take several.
        CREATE TABLE brews (
            id             INTEGER PRIMARY KEY,
            batch_id       INTEGER NOT NULL REFERENCES batches(id) ON DELETE CASCADE,
            brewhouse_id   INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            vessel_id      INTEGER REFERENCES elements(id) ON DELETE SET NULL,  -- knocked out into
            started_at     TEXT NOT NULL,
            knocked_out_at TEXT,                       -- NULL while brewing
            volume_l       REAL NOT NULL DEFAULT 0,
            og             REAL,
            notes          TEXT NOT NULL DEFAULT '',
            user_id        INTEGER NOT NULL REFERENCES users(id)
        );
        CREATE INDEX brews_by_batch ON brews (batch_id);

        -- The ingredients each brew used, as confirmed by the brewer at
        -- knock-out. Inventory deductions (Phase 5) are made from these.
        -- library_id is not a foreign key: library entries can be deleted.
        CREATE TABLE brew_ingredients (
            id          INTEGER PRIMARY KEY,
            brew_id     INTEGER NOT NULL REFERENCES brews(id) ON DELETE CASCADE,
            position    INTEGER NOT NULL,              -- order in the recipe
            kind        TEXT NOT NULL,                 -- 'fermentable', 'hop', 'yeast', 'misc'
            library_id  INTEGER,
            name        TEXT NOT NULL,
            detail      TEXT NOT NULL DEFAULT '',      -- which addition, e.g. 'Boil, 60 min'
            amount_kind TEXT NOT NULL,                 -- 'mass' (kg), 'volume' (L), 'count' (packs), 'text'
            planned     REAL NOT NULL DEFAULT 0,
            actual      REAL NOT NULL DEFAULT 0,
            text        TEXT NOT NULL DEFAULT ''       -- 'text' amounts
        );
        CREATE INDEX brew_ingredients_by_brew ON brew_ingredients (brew_id, position);

        -- The beer ledger: every change in the volume of a batch in a vessel,
        -- positive in and negative out. A vessel's contents are the sum of
        -- its rows. Append-only: corrections are new rows, never edits. TTB
        -- reports are built from this table.
        CREATE TABLE volume_events (
            id          INTEGER PRIMARY KEY,
            at          TEXT NOT NULL,                 -- when it happened
            batch_id    INTEGER NOT NULL REFERENCES batches(id),
            vessel_id   INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            volume_l    REAL NOT NULL,
            reason      TEXT NOT NULL,                 -- see VolumeReason in domain/batch.h
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX volume_events_by_batch ON volume_events (batch_id);
        CREATE INDEX volume_events_by_vessel ON volume_events (vessel_id);

        -- Gravity, temperature and pH readings.
        CREATE TABLE readings (
            id            INTEGER PRIMARY KEY,
            batch_id      INTEGER NOT NULL REFERENCES batches(id) ON DELETE CASCADE,
            vessel_id     INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            at            TEXT NOT NULL,
            method        TEXT NOT NULL,               -- 'hydrometer', 'refractometer', 'digital'
            gravity_sg    REAL,                        -- corrected
            raw_value     REAL,                        -- as read
            temperature_c REAL,
            ph            REAL,
            note          TEXT NOT NULL DEFAULT '',
            user_id       INTEGER NOT NULL REFERENCES users(id)
        );
        CREATE INDEX readings_by_batch ON readings (batch_id, at);

        -- Whether an empty vessel is clean, needs cleaning or is out of
        -- service. Vessels without a row are clean.
        CREATE TABLE vessel_conditions (
            vessel_id  INTEGER PRIMARY KEY REFERENCES elements(id) ON DELETE CASCADE,
            condition  TEXT NOT NULL,                  -- 'clean', 'dirty', 'out_of_service'
            changed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- Things to do, optionally for a piece of equipment or a batch.
        CREATE TABLE tasks (
            id         INTEGER PRIMARY KEY,
            title      TEXT NOT NULL,
            notes      TEXT NOT NULL DEFAULT '',
            element_id INTEGER REFERENCES elements(id) ON DELETE CASCADE,
            batch_id   INTEGER REFERENCES batches(id) ON DELETE CASCADE,
            due_date   TEXT,                           -- local date, YYYY-MM-DD
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
            created_by INTEGER NOT NULL REFERENCES users(id),
            done_at    TEXT,
            done_by    INTEGER REFERENCES users(id)
        );
        CREATE INDEX tasks_by_due ON tasks (done_at, due_date);

        -- What happened in the brewery, for people to read: the Log tabs.
        -- Production events are recorded here; the audit_log keeps
        -- configuration changes (equipment, recipes, settings).
        CREATE TABLE activity_log (
            id               INTEGER PRIMARY KEY,
            at               TEXT NOT NULL,
            user_id          INTEGER NOT NULL REFERENCES users(id),
            kind             TEXT NOT NULL,
            batch_id         INTEGER REFERENCES batches(id) ON DELETE CASCADE,
            element_id       INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            other_element_id INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            text             TEXT NOT NULL
        );
        CREATE INDEX activity_by_batch ON activity_log (batch_id, at);
        CREATE INDEX activity_by_element ON activity_log (element_id, at);
        CREATE INDEX activity_by_other_element ON activity_log (other_element_id, at);
    )sql"},
    {5, R"sql(
        -- Things the brewery stocks. Items with history are archived, never deleted.
        CREATE TABLE inventory_items (
            id            INTEGER PRIMARY KEY,
            name          TEXT NOT NULL,
            category      TEXT NOT NULL,               -- see ItemCategory in domain/inventory.h
            unit          TEXT NOT NULL,               -- 'mass' (kg), 'volume' (L) or 'count'
            library_id    INTEGER,                     -- the ingredient library entry it stocks (not a foreign key)
            reorder_level REAL,                        -- low stock below this
            location_id   INTEGER REFERENCES elements(id) ON DELETE SET NULL,
            notes         TEXT NOT NULL DEFAULT '',
            archived      INTEGER NOT NULL DEFAULT 0,
            created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- Deliveries: each is a lot, drawn from first in, first out.
        CREATE TABLE inventory_lots (
            id          INTEGER PRIMARY KEY,
            item_id     INTEGER NOT NULL REFERENCES inventory_items(id),
            received_at TEXT NOT NULL,
            quantity    REAL NOT NULL,                 -- as received
            supplier    TEXT NOT NULL DEFAULT '',
            lot_code    TEXT NOT NULL DEFAULT '',
            cost        REAL,
            best_before TEXT,                          -- local date, YYYY-MM-DD
            note        TEXT NOT NULL DEFAULT ''
        );
        CREATE INDEX lots_by_item ON inventory_lots (item_id, received_at);

        -- The stock ledger: every change to what's on hand, positive in and
        -- negative out. An item's stock is the sum of its rows, a lot's what's
        -- left of it. Rows without a lot are stock used before its delivery
        -- was logged (or found in a count). Append-only.
        CREATE TABLE stock_movements (
            id                 INTEGER PRIMARY KEY,
            at                 TEXT NOT NULL,
            item_id            INTEGER NOT NULL REFERENCES inventory_items(id),
            lot_id             INTEGER REFERENCES inventory_lots(id),
            quantity           REAL NOT NULL,
            reason             TEXT NOT NULL,          -- see StockReason in domain/inventory.h
            batch_id           INTEGER REFERENCES batches(id) ON DELETE SET NULL,
            brew_ingredient_id INTEGER REFERENCES brew_ingredients(id) ON DELETE SET NULL,
            note               TEXT NOT NULL DEFAULT '',
            user_id            INTEGER NOT NULL REFERENCES users(id),
            recorded_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX stock_by_item ON stock_movements (item_id, at);
        CREATE INDEX stock_by_lot ON stock_movements (lot_id);
    )sql"},
    {6, R"sql(
        -- Dry and cold storage became one element type, Inventory (still
        -- stored as 'dry_storage'). Items are no longer kept in a particular
        -- one, so inventory_items.location_id is no longer used.
        UPDATE elements SET type = 'dry_storage' WHERE type = 'cold_storage';
    )sql"},
    {7, R"sql(
        -- Deliveries are logged as invoices; each line arrives as a lot.
        CREATE TABLE invoices (
            id          INTEGER PRIMARY KEY,
            received_at TEXT NOT NULL,
            supplier    TEXT NOT NULL DEFAULT '',
            number      TEXT NOT NULL DEFAULT '',      -- the supplier's invoice number
            note        TEXT NOT NULL DEFAULT '',
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX invoices_by_date ON invoices (received_at);
        ALTER TABLE inventory_lots ADD COLUMN invoice_id INTEGER REFERENCES invoices(id);
        CREATE INDEX lots_by_invoice ON inventory_lots (invoice_id);

        -- Cans: what one holds, in liters.
        ALTER TABLE inventory_items ADD COLUMN fill_volume_l REAL;
    )sql"},
    {8, R"sql(
        -- Empty kegs in the keg cleaner: every change in how many of each
        -- size are dirty or clean, positive in and negative out. The counts
        -- are the sums. Append-only.
        CREATE TABLE keg_movements (
            id          INTEGER PRIMARY KEY,
            at          TEXT NOT NULL,
            keg_type    TEXT NOT NULL,                 -- 'half_barrel' or 'sixtel'
            state       TEXT NOT NULL,                 -- 'dirty' or 'clean'
            quantity    INTEGER NOT NULL,
            reason      TEXT NOT NULL,                 -- see KegReason in domain/packaging.h
            batch_id    INTEGER REFERENCES batches(id) ON DELETE SET NULL,  -- filled with
            note        TEXT NOT NULL DEFAULT '',
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX kegs_by_type ON keg_movements (keg_type, state);
    )sql"},
    {9, R"sql(
        -- Packaged beer: every change in how many packages of each batch
        -- there are, by kind and size of package, positive in and negative
        -- out. What's on hand is the sums. Append-only.
        CREATE TABLE packaged_movements (
            id            INTEGER PRIMARY KEY,
            at            TEXT NOT NULL,
            batch_id      INTEGER NOT NULL REFERENCES batches(id),
            package       TEXT NOT NULL,               -- 'half_barrel', 'sixtel' or 'can'
            unit_volume_l REAL NOT NULL,               -- what each package holds
            quantity      INTEGER NOT NULL,
            reason        TEXT NOT NULL,               -- see PackagedReason in domain/packaging.h
            vessel_id     INTEGER REFERENCES elements(id) ON DELETE SET NULL,  -- packaged from
            note          TEXT NOT NULL DEFAULT '',
            user_id       INTEGER NOT NULL REFERENCES users(id),
            recorded_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX packaged_by_batch ON packaged_movements (batch_id, package);
    )sql"},
    {10, R"sql(
        -- Signing in. People who leave are made inactive rather than deleted,
        -- so the records they made keep their name. `username` now holds the
        -- name in lower case, which keeps names unique whatever their case.
        ALTER TABLE users ADD COLUMN active INTEGER NOT NULL DEFAULT 1;
        ALTER TABLE users ADD COLUMN last_sign_in TEXT;
        UPDATE users SET username = lower(display_name);

        -- Each person's own preferences: theme, window layout, which windows
        -- are open and where the floor is scrolled to.
        CREATE TABLE user_settings (
            user_id INTEGER NOT NULL REFERENCES users(id),
            key     TEXT NOT NULL,
            value   TEXT NOT NULL,
            PRIMARY KEY (user_id, key)
        );

        -- Until now these were kept with the brewery's settings, for its one user.
        INSERT INTO user_settings (user_id, key, value)
            SELECT u.id, s.key, s.value FROM settings s, users u
            WHERE u.id = (SELECT MIN(id) FROM users)
              AND s.key IN ('ui.theme', 'floor.view', 'floor.open_elements', 'batches.open_windows',
                          'recipes.open_editors', 'recipes.editor_tab');
        DELETE FROM settings
            WHERE key IN ('ui.theme', 'floor.view', 'floor.open_elements', 'batches.open_windows',
                          'recipes.open_editors', 'recipes.editor_tab');
    )sql"},
    {11, R"sql(
        -- A recovery code lets someone who forgot their password choose a new
        -- one at the sign-in screen. Hashed like the password.
        ALTER TABLE users ADD COLUMN recovery_hash TEXT;
    )sql"},
    {12, R"sql(
        -- Formulas an admin has adjusted (see calc/formulas.h). The originals
        -- are part of Ninkasi, so deleting a row puts a formula back.
        CREATE TABLE formula_adjustments (
            formula    TEXT PRIMARY KEY,  -- the formula's key, such as 'abv_simple'
            latex      TEXT NOT NULL,
            user_id    INTEGER NOT NULL REFERENCES users(id),
            changed_at TEXT NOT NULL
        );
    )sql"},
    {13, R"sql(
        -- A picture under the Brewery Floor, such as the building's plan: at
        -- most one. Its height follows the picture's shape.
        CREATE TABLE floor_background (
            id        INTEGER PRIMARY KEY CHECK (id = 1),
            image     BLOB NOT NULL,      -- the file as chosen: PNG, JPEG, BMP or GIF
            file_name TEXT NOT NULL,
            x         REAL NOT NULL,      -- its top-left corner, in floor units
            y         REAL NOT NULL,
            width     REAL NOT NULL,
            opacity   REAL NOT NULL       -- 1 is solid
        );
    )sql"},
    {14, R"sql(
        -- People and businesses beer is sold to.
        CREATE TABLE customers (
            id         INTEGER PRIMARY KEY,
            name       TEXT NOT NULL,
            channel    TEXT NOT NULL,                  -- see Channel in domain/sales.h
            contact    TEXT NOT NULL DEFAULT '',
            notes      TEXT NOT NULL DEFAULT '',
            active     INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- The price list. A price applies from its date until a later price for
        -- the same thing takes over, so sales keep the prices they had.
        CREATE TABLE prices (
            id             INTEGER PRIMARY KEY,
            beer           TEXT NOT NULL DEFAULT '',   -- '' for every beer without its own price
            channel        TEXT NOT NULL,
            priced_as      TEXT NOT NULL,              -- 'package' or 'pour'
            package        TEXT NOT NULL DEFAULT '',   -- packages: 'half_barrel', 'sixtel' or 'can'
            unit_volume_l  REAL NOT NULL DEFAULT 0,    -- cans: what one holds (0 for any can)
            pack_size      INTEGER NOT NULL DEFAULT 1, -- packages in each unit sold
            pour_l         REAL NOT NULL DEFAULT 0,    -- pours: how much one is
            yield          REAL NOT NULL DEFAULT 1,    -- pours: the share of a keg sold as pours
            price          REAL NOT NULL,              -- each unit sold, in the brewery's currency
            effective_from TEXT NOT NULL,              -- local date
            user_id        INTEGER NOT NULL REFERENCES users(id),
            recorded_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
    )sql"},
    {15, R"sql(
        -- Sales: beer and anything else sold, to a customer or over the counter.
        CREATE TABLE sales (
            id          INTEGER PRIMARY KEY,
            at          TEXT NOT NULL,                 -- when it was sold
            channel     TEXT NOT NULL,                 -- see Channel in domain/sales.h
            customer_id INTEGER REFERENCES customers(id),
            number      TEXT NOT NULL DEFAULT '',      -- invoice or order number
            source      TEXT NOT NULL,                 -- see SaleSource
            import_id   INTEGER,                       -- the POS import it came from
            note        TEXT NOT NULL DEFAULT '',
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX sales_at ON sales(at);
        CREATE INDEX sales_customer ON sales(customer_id);

        CREATE TABLE sale_lines (
            id            INTEGER PRIMARY KEY,
            sale_id       INTEGER NOT NULL REFERENCES sales(id),
            kind          TEXT NOT NULL,              -- 'package', 'pour', 'deposit' or 'other'
            beer          TEXT NOT NULL DEFAULT '',
            package       TEXT NOT NULL DEFAULT '',   -- packages and deposits: 'half_barrel', 'sixtel' or 'can'
            unit_volume_l REAL NOT NULL DEFAULT 0,    -- packages: what each holds
            quantity      REAL NOT NULL,              -- packs, pours, kegs or units; deposits refunded are negative
            pack_size     INTEGER NOT NULL DEFAULT 1,
            pour_l        REAL NOT NULL DEFAULT 0,
            unit_price    REAL,                       -- each; NULL until priced
            description   TEXT NOT NULL DEFAULT ''
        );
        CREATE INDEX sale_lines_sale ON sale_lines(sale_id);

        -- Beer taken out of packaged goods for a line of a sale, which is how
        -- the cost of what was sold is traced back to its batches.
        ALTER TABLE packaged_movements ADD COLUMN sale_line_id INTEGER REFERENCES sale_lines(id);
        CREATE INDEX packaged_movements_sale_line ON packaged_movements(sale_line_id);
        -- Kegs back from a customer, so each customer's kegs out can be counted.
        ALTER TABLE keg_movements ADD COLUMN customer_id INTEGER REFERENCES customers(id);
    )sql"},
    {16, R"sql(
        -- Point-of-sale exports imported. The sales each made refer to it
        -- (sales.import_id).
        CREATE TABLE pos_imports (
            id          INTEGER PRIMARY KEY,
            file_name   TEXT NOT NULL,
            first_day   TEXT NOT NULL,   -- local dates
            last_day    TEXT NOT NULL,
            rows        INTEGER NOT NULL,
            user_id     INTEGER NOT NULL REFERENCES users(id),
            imported_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- How each item on the point of sale is recorded, so it's mapped once.
        CREATE TABLE pos_items (
            item          TEXT PRIMARY KEY COLLATE NOCASE,  -- as the point of sale names it
            kind          TEXT NOT NULL,              -- 'pour', 'package', 'other' or 'ignore'
            beer          TEXT NOT NULL DEFAULT '',
            pour_l        REAL NOT NULL DEFAULT 0,
            package       TEXT NOT NULL DEFAULT '',
            unit_volume_l REAL NOT NULL DEFAULT 0,
            pack_size     INTEGER NOT NULL DEFAULT 1
        );
    )sql"},
    {17, R"sql(
        -- What the brewery spends besides what it brews with, by category.
        CREATE TABLE expense_categories (
            id     INTEGER PRIMARY KEY,
            name   TEXT NOT NULL UNIQUE COLLATE NOCASE,
            active INTEGER NOT NULL DEFAULT 1   -- offered for new expenses
        );
        INSERT INTO expense_categories (name) VALUES
            ('Rent'), ('Wages'), ('Utilities'), ('Insurance'), ('Repairs and maintenance'),
            ('Cleaning and lab supplies'), ('Taproom supplies'), ('Marketing'), ('Licenses and fees'),
            ('Professional services'), ('Delivery and freight'), ('Other');

        CREATE TABLE expenses (
            id          INTEGER PRIMARY KEY,
            day         TEXT NOT NULL,              -- local date
            payee       TEXT NOT NULL DEFAULT '',
            category_id INTEGER NOT NULL REFERENCES expense_categories(id),
            amount      REAL NOT NULL,
            number      TEXT NOT NULL DEFAULT '',   -- invoice or receipt number
            note        TEXT NOT NULL DEFAULT '',
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        CREATE INDEX expenses_day ON expenses(day);

        -- The same amount every month, counted on the first: rent, wages...
        CREATE TABLE recurring_expenses (
            id          INTEGER PRIMARY KEY,
            category_id INTEGER NOT NULL REFERENCES expense_categories(id),
            description TEXT NOT NULL,
            monthly     REAL NOT NULL,
            first_month TEXT NOT NULL,   -- 'YYYY-MM'
            last_month  TEXT,            -- NULL while it goes on
            user_id     INTEGER NOT NULL REFERENCES users(id),
            recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
    )sql"},
    {18, R"sql(
        -- Pours are priced by volume: a pour price is now per liter (shown per
        -- fl oz or 50 mL), whatever the pour. Each becomes its price per liter.
        UPDATE prices SET price = price / pour_l WHERE priced_as = 'pour' AND pour_l > 0;
        ALTER TABLE prices DROP COLUMN pour_l;
    )sql"},
};

} // namespace

int latest_schema_version() {
    return std::end(kMigrations)[-1].version;
}

int schema_version(Database& db) {
    auto stmt = db.prepare("PRAGMA user_version;");
    stmt.step();
    return static_cast<int>(stmt.column_int64(0));
}

void migrate(Database& db) {
    migrate(db, latest_schema_version());
}

void migrate(Database& db, int version) {
    const int current = schema_version(db);
    if (current > latest_schema_version()) {
        throw DbError("This brewery's data was saved by a newer version of Ninkasi (database version " +
                      std::to_string(current) + "; this version reads up to " +
                      std::to_string(latest_schema_version()) + "). Install the latest Ninkasi to open it.");
    }
    for (const auto& migration : kMigrations) {
        if (migration.version <= current || migration.version > version) continue;
        Transaction tx(db);
        db.exec(migration.sql);
        db.exec("PRAGMA user_version = " + std::to_string(migration.version) + ";");
        tx.commit();
    }
}

} // namespace ninkasi::data
