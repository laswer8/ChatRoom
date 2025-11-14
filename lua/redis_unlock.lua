-- KEYS[1]：传入的key  ARGV[1]：传入的value
-- 判断传入的锁key是否存在，如果不存在则直接返回1，如果存在则判断传入的value值是否和获取到的value相等
if redis.call('EXISTS',KEYS[1]) == 0 then
	return 1
else
-- 判断传入的value值是否和获取到的value相等，如果相等则代表是当前线程删除锁，执行删除对应key逻辑，然后返回1，否则返回0
	if redis.call('GET',KEYS[1]) == ARGV[1] then
		return redis.call('DEL',KEYS[1])
	else
		return 0
	end
end