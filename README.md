# ETA-matris-klocka
Kod m.m för ETAs klocka baserat på två ledmatriser och tid via CAN

Ursprungligen skrivet av Sven Åkersten.

Finns även en möjlighet att visa två kaffekoppar på displayen ifall kaffebryggaren kan (över CAN?) informera när kaffet är klart. Använd inte i nuvarande kod.

Det finns en databuffer i koden som speglar varje bit som på/av på displayen. Det är helt enkelt så att man kan fylla displaybuffern med vad man vill ha på displayen och sen klocka in datan i displayen. Databuffern ska vara en tvådimensionell array där man tänker sig att varje byte motsvara 8 pixlar på en rad. Alltså enklast möjliga om man skrev ut alla talen som binära tal.
Så en 8*8 pixlar display som ska visa ett "E" längst upp till höger blir i databuffern:
0b0001 1111
0b0001 0000
0b0001 0000
0b0001 1110
0b0001 0000
0b0001 0000
0b0001 1111
0b0000 0000

P.g.a. ordningen hur pixlarna klockas in som bitar på den fysiska displayen är det lite invecklat med den koden. Detta kan testas med att att töm klocka in en bit i taget och se vilken pixel som lyser upp när man drar output latch pinnen hög. Varje byte delas nämligen in som ett 4 kolumner 2 rader stort område på varje fysisk displayenhet.

Standard C, skrivet med Arduino IDE och biblotek.

Även en mod gjort i Arduino bootloadern med högre Brown out reset spänning. Oklart vad den gör, försök att få den att sluta hänga sig ibland. CAN trancivern har en tendens att göra det tyvärr.

LED matrisdisplayerna är egentligen bara shiftregister, klockade in displaydatan med SPI enheten i Atmega328 processorn (på arduinon). Men eftersom displayerna har 2 data med en gemensam klocka, är  unde och övre halvan seriekopplad. Det går en ensam ledare från sista displayen ut tillbaka till första displayen in. Tror det är 3 st 8*8 pixlar displayer.
