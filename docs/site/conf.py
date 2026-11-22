DOXYFILE = 'Doxyfile'

LINKS_NAVBAR1 = [
    (None, 'pages', [(None, 'about'), (None, 'guarantees'), (None, 'design'), (None, 'authority'), (None, 'modulemap'), (None, 'contributing')]),
    (None, 'namespaces', []),
    # No 'files' entry on purpose. m.css renders no file page for a compound with no documentation of its own, and this project
    # documents types rather than files (there is no `@file` brief in any header), so the Files tree could only ever be a list of
    # directories pointing at nothing. Documenting forty headers to fill a menu is documentation written for the menu's sake;
    # when a header does gain an `@file` brief, put the entry back here. scripts/check_site.sh fails if an entry is a dead end.
]

# Add your own navbar links using the code below.
# To find the valid link names, you can inspect the URL of a generated documentation site.

# LINKS_NAVBAR1 = [
#     (None, 'pages', [(None, 'about')]),
#     (None, 'namespaces', [(None, 'namespacemosaic')]),
# ]
#
# LINKS_NAVBAR2 = [
#     (None, 'annotated', [(None, 'classmosaic_1_1_mosaic')]),
#     (None, 'files', [(None, 'mosaic_8h')]),
# ]
