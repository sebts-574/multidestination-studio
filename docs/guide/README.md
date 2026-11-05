# User guide

`build_guide.py` builds the PDF user guide with ReportLab:

```sh
python3 build_guide.py ../Multidestination-Studio-1.1.0-User-Guide.pdf shots
```

Screenshots go in `shots/` under the file names given in the `shot()` calls; missing ones are drawn as
placeholders showing the expected file name.
