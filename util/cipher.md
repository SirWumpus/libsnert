Cipher
======

To Build
--------

        $ cc -o cipher cipher.c


Usage Examples
--------------

        $ ./cipher
        usage: cipher [-dv][-b size] key1 key2 < input

        $ echo Hello world 123. | ./cipher broukhis cooper
        AE8C2 1AB3B A5BB4 95BBA 63BE8 31

        $ echo Hello world 123. | ./cipher broukhis cooper \
            | ./cipher -d broukhis cooper
        Hello world 123.

        $ ./cipher -d landoncurtnoll leobroukhis <<EOF
        EB438 B1867 2B4B2 03ABE 2808C 64118 1077A D8287 BD155 272B6
        B78F3 86BDB B08FB 78A20 803B6 BBF85 A718B 14B78 44588 888EC
        65B5A BBB91 CBCE6 5D611 E65A0 653B9 83805 5B8CC 48A03 4
        EOF

        $ ./cipher SimonCooper 'chongo /\cc/\' <cipher.c


Features
--------

* CT106 base 16 printable ASCII straddling checkerboard.
* Can encode text files, in 500 byte blocks (see -b option).
* The techniques can be applied manually by hand in the field.
* Frustrate the NSA and governments.


Suggested IOCCC Categories
--------------------------

* The E. Snowden Award
* Best Agent To Get Smart
* Best SPECTRE Of Intelligence Acronyms
* Our Favourite UNCLE Flint
* How To Bond With The NSA
* Shagadelic! Yeah! Baby!
* Best [READACTED]


Introduction
------------

An implementation of a [pencil-and-paper field cipher][PNP], similar to the Cold War era [VIC cipher][CIA] discovered in a [hollow nickel][FBI].  It uses a CT106 base 16 printable ASCII straddling checkerboard, columnar transposition, and disrupted triangular transposition.


Background
----------

The VIC cipher uses an English CT28 straddling checkerboard, where the most frequent English letters are assigned a single digit code and the remaining letters have two digit codes.  Other [conversion tables][CT] such as CT37 and CT46 incorporate numeric digits, extra punctuation, and a code phrase prefix.

In the original CT28 there were two places free in the table that could be used for a punctuation and number shift, where each digit in a number is repeated three times eg. 123 as 111 222 333 (some versions only repeat the digits twice).


        CT28
            0  1  2  3  4  5  6  7  8  9
          +------------------------------
          | S  E  N  O  R  I  T  A
        8 | B  C  D  F  G  H  J  K  L  M
        9 | P  Q  U  V  W  X  Y  Z  .  /

        Example:

        M EET  AT  /   1   7   3   0  /   W IND SOR  P U B .
        89116  76  99 111 777 333 000 99  945282034  90928098


This message, following the straddling checkerboard substitution, is a multiple of five digits.

In cases where the message length is not a multiple of five, padding with garbage letters can be applied.  Making the encoded output uniform makes it easy to visually verify that no digits are missing during any of the manual encoding steps or during radio transmission.  The padding also provides a minor benefit in obfuscating the message length and introduce some useless data into the message, which can make crypto analysis more difficult.

Following the message substitution using a straddling checkerboard, apply a simple columnar transposition, where the message is laid out into a matrix. The matrix width is the first key length.


        b a b y l o n 5
        ---------------
        8 9 1 1 6 7 6 9
        9 1 1 1 7 7 7 3
        3 3 0 0 0 9 9 9
        4 5 2 8 2 0 3 4
        9 0 9 2 8 0 9 8


The message is then read from the matrix column by column based on the sort order of the characters of the first key. Typically the key would be all alphabetic or all numeric, but this code assumes ASCII keys.


        5     a     b     b     l     n     o     y
        ----- ----- ----- ----- ----- ----- ----- -----
        93948 91350 89349 11029 67028 67939 77900 11082


The resulting message is now laid out into a second matrix based on a disrupted triangular transposition.  The matrix width is the length of the second key and the height is the message length divided by the second key length rounded up.

The first triangular area starts at the column that will be read first, based on character set sort order, and extends to the end of the row.  It continues in the next row one column over and extends to the end of the row.  This continues until one full row.  A second triangle area is started from the next column that will be read.  Create as many triangle areas as needed given the number of rows.  The first part of the message fills in the matrix left to right top to bottom leaving the triangle areas empty, then the triangular areas are filled in with the remainder of the message.


        Part 1                  Part 2

        d r a l l               d r a l l
        ---------               ---------
        9 3 _ _ _               9 3 6 7 0
        9 4 8 _ _               9 4 8 2 8
        9 1 3 5 _               9 1 3 5 6
        0 8 9 3 4               0 8 9 3 4
        _ _ _ _ _               7 9 3 9 7
        9 _ _ _ _               9 7 9 0 0
        1 1 _ _ _               1 1 1 1 0
        0 2 9 _ _               0 2 9 8 2


The message is then read from the matrix in the same manner as for the columnar transposition.


        a        d        l        l        r
        -------- -------- -------- -------- --------
        68393919 99907910 72539018 08647002 34189712


The encoded message is typically written out in groups of five digits for [transmission][Nbrs]:


        68393 91999 90791 07253 90180 86470 02341 89712


Decoding a message is done by applying the operations in reverse order.

The [VIC cipher][Vic1] and the modern [SECOM][SECOM] variant both describe additional steps to transform the human remembered keys into several sequences of apparently random digits, which are used as the keys for a straddling checkerboard substitution and at least two transpositions, columnar and disrupted.

The above covers the basic techniques of modern pen-and-paper ciphers.  The literature, in particular the assembled collection by [Dirk Rijmenants'][Dirk], propose additional steps to further encipher the text, such as using modular arithmetic and chain addition to apply a pseudo one-time pad sequence following the substitution, and possibly extra transpositions.


The IOCCC Cipher
----------------

This program uses a hexadecimal CT106 modelled on a US QWERTY keyboard layout of 96 printable characters and white space.  It assumes lower case alpha is predominate, with the most frequent English lower case letters using single digits, including space and linefeed.  The remaining double digit layout corresponds to the remaining QWERTY keyboard characters unshifted and shifted, plus ASCII white space and escape codes.

Note that there are many variations on the CT106 layout possible; the only criteria are that it should be easy to remember or reconstitute in the field and have a good distribution of the printable characters.


        CT106 Base 16 Printable ASCII Straddling Checkerboard

            0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
          +------------------------------------------------
          | s  e  n  o  r  i  t  a  SP LF
        A | `  1  2  3  4  5  6  8  8  9  0  -  =  q  w  y
        B | u  p  [  ]  \  d  f  g  h  j  k  l  ;  '  z  x
        C | c  v  b  m  ,  .  /  ~  !  @  #  $  %  ^  &  *
        D | (  )  _  +  Q  W  E  R  T  Y  U  I  O  P  {  }
        E | |  A  S  D  F  G  H  J  K  L  :  "  Z  X  C  V
        F | B  N  M  <  >  ?  BS HT VT FF CR ES

        Substitution example:

        #include <stdio.h>

        int
        main(int argc, char **argv)
        {
                printf("Hello world 123.\\n");
                return 0;
        }

        CA52C 0BBB0 B518F 306B5 53C5B 8F499 5269C 3752D 05268 74B7C
        0C48C 0B874 8CFCF 74B7C 1D19D E9F7B 14526 B6D0E BE61B BBB38
        AE34B BB58A 1A2A3 C5B42 EBD1B C9F74 16B04 28AAB C9DF9


This program only handles the substitution and two transpositions steps using two printable ASCII keys, each between 1 and 255 characters in length supplied by the user.  For best cryptographic results the two keys should be of different lengths, ideally 10 or more.

This cipher supports encoding messages of any length, using a default of 500 byte blocks (use -b to specify a different block size).  As the clear text message is read, the CT106 substitution is applied until the block buffer is filled with the encoded hex digits.  If the encoding of the last character read overflows the buffer, then the character is pushed back for the start of the next encoding block and the partially encoded hex digit is left in the buffer as padding. The block is then subjected to the two transpositions.

After the last CT106 substitution step, there is no padding of the output to multiples of four digits.  The sender can pad the message manually with garbage letters or whitespace, if desired, to achieve uniform output, which allows for visual error checking when working by hand.

Decoding is simply the reverse procedure. The encoded message is read in 500 ASCII hex digit blocks, discarding characters that are not ASCII hex digits. The two transpositions are reversed and the resulting ASCII hex block decoded based on the CT106. If the last hex digit of the block results in an incomplete decoding, it is discarded, and the next block read. The process is repeated until the message is consumed.

The -v option writes to standard error verbose debug output showing the intermediate steps used to encode/decode a message.

Given a known CT106 layout and messages length less than equal to the block size, the strength of this cipher is based on the length of the two keys used for the transpositions ( length(key1)! * length(key2)! ), since the key characters themselves do not factor into the encoding as they would if broken down into bits based on their ASCII codes as with Base64 and other digital encryption methods.  For longer messages the block size will also factor into the cipher strength.

This project evolved out of curiosity for secret codes and spy craft, but also today's real world need to protect personal privacy from people like governments, lawyers, moms, jealous pet fish named Eric, whether communicating by radio, SMS, email, dropbox.com, or old school hard copy.


References
----------

"Number One From Moscow"  
<https://www.cia.gov/library/center-for-the-study-of-intelligence/kent-csi/vol5no4/html/v05i4a09p_0001.htm>

"Rudolph Ivanovich Abel (Hollow Nickel Case)"  
<http://www.fbi.gov/about-us/history/famous-cases/hollow-nickel>

"Straddling Checkerboards"  
<http://users.telenet.be/d.rijmenants/en/table.htm>

"Cipher Machines and Cryptology"  
<http://users.telenet.be/d.rijmenants/>

"Hand Ciphers"  
<http://users.telenet.be/d.rijmenants/en/handciphers.htm>

"Numbers Stations"  
<https://en.wikipedia.org/wiki/Numbers_station>

"The SECOM Cipher"  
<http://users.telenet.be/d.rijmenants/en/secom.htm>

"VIC Cipher"  
<http://en.wikipedia.org/wiki/VIC_cipher>


[CIA]:  https://www.cia.gov/library/center-for-the-study-of-intelligence/kent-csi/vol5no4/html/v05i4a09p_0001.htm "Number One From Moscow"

[FBI]:  http://www.fbi.gov/about-us/history/famous-cases/hollow-nickel  "Rudolph Ivanovich Abel (Hollow Nickel Case)"

[CT]:   http://users.telenet.be/d.rijmenants/en/table.htm       "Straddling Checkerboards"

[Dirk]: http://users.telenet.be/d.rijmenants/ "Cipher Machines and Cryptology"

[PNP]:  http://users.telenet.be/d.rijmenants/en/handciphers.htm         "Hand Ciphers"

[Nbrs]: https://en.wikipedia.org/wiki/Numbers_station "Numbers Stations"

[SECOM]:http://users.telenet.be/d.rijmenants/en/secom.htm "The SECOM Cipher"

[Vic1]: http://en.wikipedia.org/wiki/VIC_cipher "VIC Cipher"

