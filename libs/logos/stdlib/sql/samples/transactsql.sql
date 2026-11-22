DECLARE @total INT = 0;
SELECT TOP (10) name, id FROM sys.objects WHERE type = 'U' ORDER BY name;
IF @total > 0
BEGIN
    UPDATE dbo.orders SET status = 'shipped' WHERE id = 5;
END
ELSE
    PRINT 'nothing';
EXEC sp_helptext 'dbo.orders';
