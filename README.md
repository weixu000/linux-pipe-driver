# linux-pipe-driver
Course project for operating systems which replicates builtin pipes.
It is a Linux driver which, when installed, will manage two device files `/dev/mypipe/mypipe_in` and `/dev/mypipe/mypipe_out`.
One can write to `/dev/mypipe/mypipe_in` and read the same data from `/dev/mypipe/mypipe_in`.

For demonstration, there are two user-space programs `writer.c` which writes numbers to `/dev/mypipe/mypipe_in` in an infinite loop and `reader.c` which reads and prints from `/dev/mypipe/mypipe_out`.

You may refer to `doc.pdf` for details (in Chinese).
