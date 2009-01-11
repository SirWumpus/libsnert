/socketOpen/ {
        offset = index($0, "s.fd=");
        fd = substr($0, offset+5);
        active[fd] = $0;
}

/socketAccept/ {
        offset = index($0, "c.fd=");
        fd = substr($0, offset+5);
        active[fd] = $0;
}

/socketClose/ {
        offset = index($0, "s.fd=");
        fd = substr($0, offset+5);
        delete active[fd];
}

END {
        for (el in active)
                printf("%s\n", active[el]);
}
