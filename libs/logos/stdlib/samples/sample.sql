-- Orders per customer
SELECT c.name, count(*) AS orders, SUM(o.total) /* sum */ FROM customers c
  JOIN orders o ON o.customer_id = c.id
 WHERE o.created >= '2024-01-01' AND c.name LIKE 'A%' OR c."Quoted Name" IS NOT NULL
 GROUP BY c.name HAVING count(*) > 1 ORDER BY orders DESC LIMIT 10;

insert into t (a, b) values (1, 'it''s'), (2, ?);
UPDATE t SET a = a + 1 WHERE b = :name;
