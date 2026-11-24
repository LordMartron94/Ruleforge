-- Orders per customer
SELECT c.name, count(*) AS orders, sum(o.total) FROM customers c
  JOIN orders o ON o.customer_id = c.id
 WHERE o.created >= '2024-01-01' AND c.name LIKE 'A%'
 GROUP BY c.name HAVING count(*) > 1 ORDER BY orders DESC LIMIT 10;
INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'it''s');
CREATE TABLE IF NOT EXISTS u (id INTEGER PRIMARY KEY, name TEXT NOT NULL);
UPDATE t SET a = a + 1 WHERE b IS NOT NULL;
WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM cnt WHERE x < 10) SELECT x FROM cnt;
/* block comment */ DELETE FROM t;
