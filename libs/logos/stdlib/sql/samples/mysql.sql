-- one-table syntax
delete from t1 where col1 = true and (col2 - col3 <= (select count(*) from t2) or maincol/2 > 100.2);
select a, b from t order by a limit 10;
insert into t (a, b) values (1, 'x'), (2, 'y');
create table u (id int primary key auto_increment, name varchar(20) not null);
update t set a = a + 1 where b is not null;
