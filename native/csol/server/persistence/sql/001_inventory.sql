-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run explicitly as the database owner in the intended database. No game
-- process runs migrations, creates users, or alters pg_hba.conf/listeners.
BEGIN;
CREATE SCHEMA IF NOT EXISTS goldcraft_inventory;
REVOKE ALL ON SCHEMA goldcraft_inventory FROM PUBLIC;
CREATE TABLE IF NOT EXISTS goldcraft_inventory.schema_version (
    singleton boolean PRIMARY KEY DEFAULT true CHECK(singleton),
    version integer NOT NULL CHECK(version=1),
    ledger uuid NOT NULL DEFAULT gen_random_uuid()
);
INSERT INTO goldcraft_inventory.schema_version(singleton,version) VALUES(true,1)
ON CONFLICT(singleton) DO NOTHING;
CREATE TABLE IF NOT EXISTS goldcraft_inventory.asset (
    name text PRIMARY KEY CHECK(name ~ '^[a-z0-9_]{1,48}$'),
    -- Original item identity is supplied only after the MP contract is known.
    original_item_id integer NOT NULL UNIQUE CHECK(original_item_id>0)
);
CREATE TABLE IF NOT EXISTS goldcraft_inventory.account (
    steam_id bigint PRIMARY KEY CHECK((steam_id >> 32)=17825793 AND (steam_id & 4294967295)>0),
    coins bigint NOT NULL DEFAULT 0 CHECK(coins BETWEEN 0 AND 9000000000000000),
    revision bigint NOT NULL DEFAULT 0 CHECK(revision>=0)
);
CREATE TABLE IF NOT EXISTS goldcraft_inventory.item (
    steam_id bigint NOT NULL REFERENCES goldcraft_inventory.account,
    asset text NOT NULL REFERENCES goldcraft_inventory.asset,
    quantity bigint NOT NULL DEFAULT 0 CHECK(quantity BETWEEN 0 AND 9000000000000000),
    PRIMARY KEY(steam_id,asset)
);
CREATE TABLE IF NOT EXISTS goldcraft_inventory.event (
    id uuid PRIMARY KEY,
    steam_id bigint NOT NULL REFERENCES goldcraft_inventory.account,
    operation smallint NOT NULL CHECK(operation BETWEEN 1 AND 4),
    asset text NOT NULL,
    amount bigint NOT NULL CHECK(amount BETWEEN 1 AND 9000000000000000),
    reference uuid,
    outcome text NOT NULL CHECK(outcome IN ('applied','insufficient','invalid')),
    coins_after bigint NOT NULL,
    item_after bigint NOT NULL,
    revision_after bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CHECK((operation=4)=(reference IS NOT NULL))
);
CREATE UNIQUE INDEX IF NOT EXISTS one_refund_per_debit
ON goldcraft_inventory.event(reference) WHERE operation=4 AND outcome='applied';

CREATE OR REPLACE FUNCTION goldcraft_inventory.apply(
    p_ledger uuid,p_event uuid,p_steam bigint,p_operation smallint,
    p_asset text,p_amount bigint,p_reference uuid DEFAULT NULL)
RETURNS TABLE(outcome text,coins bigint,item_count bigint,revision bigint,replayed boolean)
LANGUAGE plpgsql SECURITY DEFINER SET search_path=pg_catalog AS $function$
DECLARE
    saved goldcraft_inventory.event%ROWTYPE;
    source goldcraft_inventory.event%ROWTYPE;
    current_coins bigint; current_item bigint:=0; current_revision bigint;
    result text:='applied';
BEGIN
    IF p_ledger IS DISTINCT FROM (SELECT ledger FROM goldcraft_inventory.schema_version WHERE singleton AND version=1) THEN
        RETURN QUERY SELECT 'wrong_ledger',0::bigint,0::bigint,0::bigint,false; RETURN;
    END IF;
    IF p_event IS NULL OR p_event='00000000-0000-0000-0000-000000000000'::uuid OR p_steam IS NULL OR (p_steam >> 32)<>17825793 OR (p_steam & 4294967295)=0
        OR p_operation IS NULL OR p_operation NOT BETWEEN 0 AND 4 OR p_asset IS NULL OR p_amount IS NULL
        OR p_amount<0 OR p_amount>9000000000000000
        OR (p_operation=0 AND (p_amount<>0 OR p_reference IS NOT NULL))
        OR (p_operation<>0 AND p_amount=0)
        OR (p_operation=1 AND p_asset<>'')
        OR (p_operation IN (2,3,4) AND p_asset !~ '^[a-z0-9_]{1,48}$')
        OR ((p_operation=4)<>(p_reference IS NOT NULL)) OR p_reference=p_event THEN
        RETURN QUERY SELECT 'invalid',0::bigint,0::bigint,0::bigint,false; RETURN;
    END IF;
    -- Lock an event before an account so retransmissions using a different
    -- account cannot apply twice. Hash collisions only serialize operations.
    PERFORM pg_advisory_xact_lock(hashtextextended(p_event::text,0));
    SELECT * INTO saved FROM goldcraft_inventory.event WHERE id=p_event;
    IF FOUND THEN
        IF saved.steam_id=p_steam AND saved.operation=p_operation AND saved.asset=p_asset AND
            saved.amount=p_amount AND saved.reference IS NOT DISTINCT FROM p_reference THEN
            RETURN QUERY SELECT saved.outcome,saved.coins_after,saved.item_after,saved.revision_after,true;
        ELSE
            RETURN QUERY SELECT 'conflict',0::bigint,0::bigint,0::bigint,true;
        END IF; RETURN;
    END IF;
    INSERT INTO goldcraft_inventory.account(steam_id) VALUES(p_steam) ON CONFLICT DO NOTHING;
    SELECT a.coins,a.revision INTO current_coins,current_revision
        FROM goldcraft_inventory.account a WHERE steam_id=p_steam FOR UPDATE;
    IF p_asset<>'' THEN
        IF NOT EXISTS(SELECT 1 FROM goldcraft_inventory.asset WHERE name=p_asset) THEN
            RETURN QUERY SELECT 'invalid',current_coins,0::bigint,current_revision,false; RETURN;
        END IF;
        INSERT INTO goldcraft_inventory.item(steam_id,asset) VALUES(p_steam,p_asset) ON CONFLICT DO NOTHING;
        SELECT quantity INTO current_item FROM goldcraft_inventory.item WHERE steam_id=p_steam AND asset=p_asset;
    END IF;
    IF p_operation=0 THEN
        RETURN QUERY SELECT 'applied',current_coins,current_item,current_revision,false; RETURN;
    ELSIF p_operation=1 THEN
        IF p_amount>9000000000000000-current_coins THEN result:='invalid';
        ELSE current_coins:=current_coins+p_amount; END IF;
    ELSIF p_operation=2 THEN
        IF p_amount>9000000000000000-current_item THEN result:='invalid';
        ELSE current_item:=current_item+p_amount; END IF;
    ELSIF p_operation=3 THEN
        IF current_item<p_amount THEN result:='insufficient';
        ELSE current_item:=current_item-p_amount; END IF;
    ELSE
        SELECT * INTO source FROM goldcraft_inventory.event WHERE id=p_reference;
        IF NOT FOUND OR source.operation<>3 OR source.outcome<>'applied' OR source.steam_id<>p_steam
            OR source.asset<>p_asset OR source.amount<>p_amount
            OR EXISTS(SELECT 1 FROM goldcraft_inventory.event e WHERE e.reference=p_reference AND e.operation=4 AND e.outcome='applied')
            OR p_amount>9000000000000000-current_item THEN result:='invalid';
        ELSE current_item:=current_item+p_amount; END IF;
    END IF;
    IF result='applied' THEN
        current_revision:=current_revision+1;
        UPDATE goldcraft_inventory.account a SET coins=current_coins,revision=current_revision WHERE steam_id=p_steam;
        IF p_asset<>'' THEN UPDATE goldcraft_inventory.item SET quantity=current_item WHERE steam_id=p_steam AND asset=p_asset; END IF;
    END IF;
    INSERT INTO goldcraft_inventory.event(id,steam_id,operation,asset,amount,reference,outcome,coins_after,item_after,revision_after)
        VALUES(p_event,p_steam,p_operation,p_asset,p_amount,p_reference,result,current_coins,current_item,current_revision);
    RETURN QUERY SELECT result,current_coins,current_item,current_revision,false;
END
$function$;
REVOKE ALL ON ALL TABLES IN SCHEMA goldcraft_inventory FROM PUBLIC;
REVOKE ALL ON FUNCTION goldcraft_inventory.apply(uuid,uuid,bigint,smallint,text,bigint,uuid) FROM PUBLIC;
COMMIT;
-- Record this UUID in the private server config. It prevents replaying a
-- durable outbox against a different database after an accidental reconnect.
SELECT version,ledger FROM goldcraft_inventory.schema_version;

-- Separately grant only USAGE and EXECUTE to the chosen runtime role:
-- GRANT USAGE ON SCHEMA goldcraft_inventory TO your_runtime_role;
-- GRANT EXECUTE ON FUNCTION goldcraft_inventory.apply(uuid,uuid,bigint,smallint,text,bigint,uuid) TO your_runtime_role;
-- Runtime role must not own the schema, function, or tables and must not be
-- superuser. Migration/asset catalog administration remains a separate role.
