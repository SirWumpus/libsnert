function table_dump(name, table, ...)
	if table == nil then
		return
	end

 	local len = 0;
	local indent = arg[1] or ""

	if table.__seen__ then
		syslog.debug(indent..name.." = (seen) ".. tostring(table));
		return
 	end

	syslog.debug(indent..name..' '.. tostring(table).." = {")
	for k,v in pairs(table) do
		len = len + 1

		local kt = type(k)
		if not (kt == 'string' or kt == 'number') then
			-- Key might be something like a thread, function, userdata.
			k = tostring(k)
		end

		local vt = type(v)
		if vt == "table" then
			table.__seen__ = true
			table_dump(k, v, indent..' ')
			table.__seen__ = nil
		elseif vt == "string" then
			syslog.debug(indent..' '..k.." = \""..v.."\"")
		elseif vt == "number" then
			syslog.debug(indent..' '..k.." = "..v)
		elseif vt == "boolean" then
			if v then v = "true" else v = "false" end
			syslog.debug(indent..' '..k.." = "..v)
		else
			syslog.debug(indent..' '..k.." = ".. vt);
		end
	end
	if 0 < # table then len = # table end
	syslog.debug(indent.."} -- "..name.." len=".. len)
end
