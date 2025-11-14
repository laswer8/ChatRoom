-- KEYS[1]：传入的key  ARGV[1]：传入的value  ARGV[2]：传入的过期时间
-- 通过SETNX命令设置锁，如果设置成功则添加一个过期时间并且返回1，否则判断是否为重入锁
if redis.call('SETNX', KEYS[1], ARGV[1]) == 1 then
	redis.call('PEXPIRE', KEYS[1], tonumber(ARGV[2]))
	return 1
else
-- 当锁已经存在时，判断传入的value是否相等，如果相等代表为重入锁返回1并且重置过期时间，否则返回0
	if redis.call('GET', KEYS[1]) == ARGV[1] then
		redis.call('PEXPIRE', KEYS[1], tonumber(ARGV[2]))
		return 1
	else
		return 0
	end
end