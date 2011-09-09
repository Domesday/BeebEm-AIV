/****************************************************************
BeebEm-AIV - BBC Micro and Master 128 Emulator
Copyright (C) David Holdsworth

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public 
License along with this program; if not, write to the Free 
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/
import javax.imageio.ImageIO;
import java.io.File;
import java.io.RandomAccessFile;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.io.FileInputStream;
import java.io.OutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.awt.image.ColorModel;
import java.awt.image.BufferedImage;
import java.net.URL;
import java.net.URLConnection;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;

public class Decompress
// exploring javax.imagio
{  private static final int LVRAW_IM_W = 768;   /* The Width of the Raw LV image */
   private static final int LVRAW_IM_H = 576;   /* The Height of the Raw LV image */
   private static final int LVRAW_BWIDTH = 3;   /* The number of bytes per pixel */

   private static final int LV_IM_MARGIN = 24;  /* The number of pixels to trim from the L and R */
   private static final int LVSCALE_IM_W = 780; /* The width of the scaled LV image */
   private static final int LVSCALE_IM_H = 576; /* The height of the scaled LV image */

   private static final int BBC_IM_XOFF = 72;   /* X offset of the BBC image */
   private static final int BBC_IM_YOFF = 32;   /* Y offset of the BBC image */
   private static final int BBC_IM_W = 640;     /* The width of the BBC image */
   private static final int BBC_IM_H = 256;     /* The height of the BBC image */
   private static final int dimFileSize = LVRAW_IM_W * LVRAW_BWIDTH * LVSCALE_IM_H
                            + BBC_IM_YOFF + BBC_IM_YOFF;      /* not sure of the YOFF terms */
   private static final int rasterSize = (LVRAW_IM_W - (2*LV_IM_MARGIN)) * LVSCALE_IM_H;

   private static final byte[][] dimHeader = { "HTTP/1.0 200 OK\r\nContent-type: image/BBC0\r\n\r\n".getBytes(),
                                       "HTTP/1.0 200 OK\r\nContent-type: image/BBC1\r\n\r\n".getBytes(),
                                       "HTTP/1.0 200 OK\r\nContent-type: image/BBC2\r\n\r\n".getBytes()
                                     };
   private static final byte[] datHeader = ("HTTP/1.0 200 OK\r\nContent-length: 4096\r\n"
                                               + "Content-type: application/octet-stream\r\n\r\n").getBytes();
   private static final byte[] jpgHeader = ("HTTP/1.0 200 OK\r\nContent-type: image/jpeg\r\n\r\n").getBytes();
   private static final boolean makeCache = false;
   private static String fileBase = null;

   private static final byte[] copyText =
      ("HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n"
          + "<HTML><BODY>All images will be copied to local disk"
             + "</BODY></HTML>\n").getBytes();

   private static final byte[] noCopyText =
      ("HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n"
          + "<HTML><BODY><FONT COLOR=#FF0000>No</FONT>images will be copied to local disk"
             + "</BODY></HTML>\n").getBytes();

   private static final byte[] helpText =
      ("HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n"
          + "<HTML><BODY>Help info"
             + "</BODY></HTML>\n").getBytes();

   private static void deliverImage(BufferedImage im, OutputStream outstr)
               throws IOException
   {  int width = im.getWidth();
      int height = im.getHeight();

      int[] res = im.getRGB(0, 0, width, height, null, 0, width);

      byte[] wb = new byte[width*LVRAW_BWIDTH];
      int i, j, n, wi;

   // determine the type of image that we have
      if  ( width >= 700 )        // must be generated form old DMS-type .dim file
         i = 0;                   // type 0 indicates scale parameters as built in to the emulator
      else if  ( width >= 600 )   // must be large 696x522 image from National Archives
         i = 1;                   // type 1 indicates scale parameters on first line of imagesize.dat file
      else                        // must be small 400x300 image from National Archives
         i = 2;                   // type 2 indicates scale parameters on second line of imagesize.dat file
      outstr.write(dimHeader[i]); // so send the appropriate header

      for (int y=0; y<height; y++) // send out the data line by line
      {  n = width;
         i = y * width;
         while  ( --n >= 0 )       // copy a line of pixels
         {  j = LVRAW_BWIDTH;
            wi = res[i+n];
            while  ( --j >= 0 )
            {  wb[LVRAW_BWIDTH * n + 2 - j] = (byte)(wi & 0xFF);
               wi = wi >> 8;
            }
         }
         outstr.write(wb);         // send to client
      }
   }

   private static String zcacheFileName(String req)
   // probably to be superceded
   {  int i = req.lastIndexOf('/');
      if  ( i+3 > req.length() )
         return req + ".jpg";
      String dirName = req.substring(0,i+3);
      File dir = new File(dirName);    // sub-dir is 1st 2 digits
      if  ( dir.exists() )
         return dirName +  req.substring(i) + ".jpg";
      return req + ".jpg";
   }

   private static String cacheFileName(String req)
   // get filename in cache treee (subdirectories have 1st 2 digits as name
   {  int i = req.lastIndexOf('/');
      if  ( i+3 > req.length() )
         return req + ".jpg";
      return req.substring(0,i+3) +  req.substring(i) + ".jpg";
   }

   public static void main(String[] args)
   {  try
      {  int ipPort = 8181;
         String remoteFunction = "/LVROM/jpg/";   // for DH test system use "/LVROM/jpg/" -- official is "/"
         String remoteHost = ".";                 // images held on "domesday1986.leeds.ac.uk";
         boolean mirrorCache = false;
         if  ( args.length > 0 )
            remoteHost = args[0];
         if  ( args.length > 1 )
            ipPort = Integer.parseInt(args[1]);
         if  ( args.length > 2 )
         {  remoteFunction = args[2];  // supplied as 3rd parameter
            if  ( mirrorCache = remoteFunction.charAt(0) == '!' )   // special for mirror cache on server
               remoteFunction = remoteFunction.substring(1);        // rest of param is path to cache directory
         }                                                          // track number 12345 is replaced by 12/12345.jpg
         if  ( args.length > 3 )
            fileBase = args[3];        // supplied as 4th parameter
         boolean copyingImages = false;
         ServerSocket listen = new ServerSocket(ipPort);
         System.out.println("Listening on port " + ipPort + "   Collecting images from " + remoteHost);
         boolean keepGoing = true;
         OutputStream outstr = null;
         if  ( remoteHost.equals(".") )
         {  remoteHost = null;         // read local files for demos and tests
            fileBase = ".";            // .ldi file must be in current directory
         }
         while  ( keepGoing )
            try
            {  Socket client = listen.accept();
               InputStream req = client.getInputStream();
               outstr = client.getOutputStream();
               byte[] buff = new byte[4096];
               int n = req.read(buff);
               int i = 0;
               while  ( buff[++i] >= (byte)' ' ) ;
               String sreq = new String(buff, 0, i);
               i = sreq.lastIndexOf(" HTTP/1");

               System.out.println("Req: " + sreq);

               if  ( sreq.startsWith("GET /LVROM/dat") )
               {  n = sreq.lastIndexOf('/', i);
                  String ws = sreq.substring(n+1,i);
                  int blockingFactor = Integer.parseInt(sreq.substring(14,16));
                  int block = Integer.parseInt(ws) & (-blockingFactor);
                  File cacheCopy;
                  outstr.write(datHeader);

                  if  ( fileBase != null )                   // reading from whole .ldi file
                  {  ws = fileBase + sreq.substring(16,n) + ".ldi";
                     File f = new File(ws);
                     System.out.println("Opening ldi file: " + f.getAbsolutePath());
                  // System.out.println("Blocking factor: " + blockingFactor);
                     RandomAccessFile fin = null;
                     if  ( f.exists() )                      // file exists so open should be OK, but ...
                        fin = new RandomAccessFile(f, "r");  // ... here we take the last chance to raise ...
                                                             // ... an exception before changing the content-type
                     if  ( (n = blockingFactor*256) > 4096 ) // if buff not big snough
                        buff = new byte[n];
                     if  ( fin != null )                     // if we opened the file OK
                     {  fin.seek((block&(-blockingFactor))*256); // mask off the bottom bits
                        i = 0;                               // amount read so far
                        while  ( i < n  &&  (block = fin.read(buff, i, n-i)) > 0 )
                           i += block;
                        fin.close();
                     }
                     outstr.write(buff);                     // send to client
                  }
                  else if  ( (cacheCopy = new File("s" + block + ".ldi")).exists() )
                  {  System.out.println("Opening: " + cacheCopy);
                     InputStream fin = new FileInputStream(cacheCopy);
                     while  ( (n = fin.read(buff)) > 0 )
                        outstr.write(buff, 0, n);
                     fin.close();
                  }
                  else if  ( remoteHost == null )            // off-line demo mode - file missing
                     outstr.write(buff, 0, n);               // N.B. buff zeroised on creation
                  else
                  {  ws = "http://" + remoteHost + sreq.substring(4,i);
                     System.out.println("Opening: " + ws);
                     InputStream fin = (new URL(ws)).openConnection().getInputStream();
                     if  ( makeCache )
                     {  OutputStream fout = new FileOutputStream(cacheCopy);
                        while  ( (n = fin.read(buff)) > 0 )
                        {  outstr.write(buff, 0, n);
                           fout.write(buff, 0, n);
                        }
                        fout.close();
                     }
                     else
                     {  while  ( (n = fin.read(buff)) > 0 )
                           outstr.write(buff, 0, n);
                     }
                     fin.close();
                  }
               }
               else if  ( sreq.startsWith("GET /LVROM/") )   // must be either dim or jpg
               {  BufferedImage im;
                  String ws;
                  File fin;
                  InputStream isJpeg;
                  if  ( (fin = new File(ws = cacheFileName(sreq.substring(15,i)))).exists() )  // local cached file
                  {  if  ( fin.length() == 0 )      // cached blank
                        isJpeg = new FileInputStream("blank.jpg");
                     else
                        isJpeg = new FileInputStream(fin);
                  }
                  else if  ( remoteHost == null )   // reading from local files, so supply a blank
                     isJpeg = new FileInputStream(ws = "blank.jpg");   // ... or should it be a dud?

                  else                              // read from remote server
                  {  if  ( mirrorCache )
                        ws = "http://" + remoteHost + remoteFunction + ws;   // accessing cache on server
                     else
                        ws = "http://" + remoteHost + remoteFunction + sreq.substring(15,i);
                     URLConnection urlc = new URL(ws).openConnection();
                     isJpeg = urlc.getInputStream();
                     if  ( urlc.getContentLength() == 0 )     // zero-length file indicates blank screen
                     {  if  ( copyingImages )                 // create a zero length cache file ...
                           new FileOutputStream(fin).close(); // ... to make fore future blank indicator
                        isJpeg.close();
                        isJpeg = new FileInputStream("blank.jpg");
                     }
                     else
                     {  if  ( copyingImages )                // read from remove server and copy to local file
                        {  FileOutputStream fout = new FileOutputStream(fin);
                           while  ( (n = isJpeg.read(buff)) > 0 )
                              fout.write(buff, 0, n);
                           isJpeg.close();
                           fout.close();
                           isJpeg = new FileInputStream(fin);
                        }
                     }
                  }
                  System.out.println("Opened: " + ws);
                  if  ( sreq.startsWith("GET /LVROM/dim") )
                  {  im = ImageIO.read(isJpeg);
                     if  ( im == null )
                     {  isJpeg.close();
                        im = ImageIO.read(isJpeg = new FileInputStream("dud.jpg"));
                     }
                     deliverImage(im, outstr);
                  }
                  else if  ( sreq.startsWith("GET /LVROM/jpg") )
                  {  outstr.write(jpgHeader);
                     while  ( (n = isJpeg.read(buff)) > 0 )
                        outstr.write(buff, 0, n);
                  }
                  else
                     throw new IOException("Invalid request " + sreq);
                  isJpeg.close();
               }
               else if  ( sreq.startsWith("GET /COPY") )
               {  copyingImages = true;
                  outstr.write(copyText);
               }
               else if  ( sreq.startsWith("GET /NOCOPY") )
               {  copyingImages = false;
                  outstr.write(noCopyText);
               }
               else if  ( sreq.startsWith("GET /STOP")  &&  ((InetSocketAddress)client.getRemoteSocketAddress()).getAddress().isLoopbackAddress() )
               {  outstr.write("HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n<H1>Stopped</H1>\n<PRE>\n".getBytes());
                  outstr.write(buff, 0, i);
                  keepGoing = false;
               }
               else if  ( sreq.startsWith("GET /HELP") )
                  outstr.write(helpText);
               else
               {  outstr.write("HTTP/1.0 403 Forbidden\r\nContent-type: text/html\r\n\r\n<H1>Access denied</H1>\n<PRE>\n".getBytes());
                  outstr.write(buff, 0, i);
               }
               outstr.flush();
               Thread.sleep(10);   // seems to be a race problem in some environments
            }
            catch(Throwable x)
            {  x.printStackTrace();
            }
            finally
            {  if  ( outstr != null )
                  outstr.close();
               outstr = null;
            }
         }
      catch(Throwable x)
      {  x.printStackTrace();
      }
   }

}
