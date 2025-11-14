local key=KEYS[1]

if redis.call('del',key) == 0 then
	return 0
end
return 1