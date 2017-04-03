import re

def get_imports(source):
    imports = []
    i = 0
    while i < len(source)-1:
        print i
        # Find the next import
        i = source.find("#import", i, len(source))
        if i == -1:
            break
        clip_start = i
        i += + len("#import")
        
        # Start clip
        import_value = ""
        
        # Skip any whitespave
        while source[i] == ' ':
            i += 1

        # Match "
        print i
        assert source[i] == '"'
        
        # Extract value
        i += 1
        import_value = ""
        while source[i] != '"':
            import_value += source[i]
            i += 1
        i += 1

        # End clip
        clip_end = i
        print source[clip_start:clip_end]
        imports.append((clip_start, clip_end, import_value))
    
    return imports

source = ""
with open('doc.md') as source_doc:
    source = source_doc.read()

imports = get_imports(source)
offset = 0
for (start, end, value) in imports:
    # value = 'src/' + value
    print "Importing: {}".format(value)
    with open(value, 'r') as import_file:
        insert = import_file.read()
        source = source[:start-offset] + insert + source[end-offset:]
        offset += end - start - len(insert)

with open('doc_gen.md', 'w') as doc:
    doc.write(source)