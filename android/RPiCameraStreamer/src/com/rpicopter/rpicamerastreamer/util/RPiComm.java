package com.rpicopter.rpicamerastreamer.util;

import java.io.DataOutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.Timer;
import java.util.TimerTask;

import com.rpicopter.rpicamerastreamer.Callback;

import android.util.Log;

public class RPiComm {
	public String error;
	public int status = 0;
	private Socket sock;
	private InetSocketAddress addr;
	private byte [] my_ip;
	private int my_port;
	private DataOutputStream out;
	private Callback context;
	
	public RPiComm(Callback c, byte []rpi_ip, int rpi_port, byte []my_ip, int my_port) {
		context = c;
		try {
			InetAddress rpi = InetAddress.getByAddress(rpi_ip);
			addr = new InetSocketAddress(rpi,rpi_port);
			this.my_ip = my_ip;
			this.my_port = my_port;
			
		} catch (Exception ex) {
			error = ex.toString();
			status = -1;
			context.notify(0, error);
		}
		
		//Timer timer = new Timer();
		//timer.schedule(new Ping(), 0, 1000);
	}
	
	private void _start() {
		try {	
			sock = new Socket();
			sock.connect(addr,2500);
			out=new DataOutputStream(sock.getOutputStream());
			
			//len 4
			//type 1
			//ip 4
			//port 4
			byte [] buf = new byte[13];
			ByteBuffer b = ByteBuffer.wrap(buf);
			b.putInt(13);
			b.put((byte)0);
			b.put(my_ip);
			b.putInt(my_port);
			out.write(buf);
			out.flush();
			//sock.close();
		} catch (Exception ex) {
			error = ex.toString();
			status = -1;
			context.notify(0, error);
		}
	}
	
	public void start() {
		new Thread(new Runnable(){
		    @Override
		    public void run() {
		    	_start();
		    }
		}).start();

	}
	
	public void _stop() {
		if (sock==null) return;
		if (!sock.isConnected()) return;
		try {
			//len 4
			//type 1
			byte [] buf = new byte[5];
			ByteBuffer b = ByteBuffer.wrap(buf);
			b.putInt(5);
			b.put((byte)1);
			out.write(buf);
			out.flush();
			
			sock.close();
		} catch (Exception ex) {
			error = ex.toString();
			status = -1;
		}
	}
	
	public void stop() {
		new Thread(new Runnable(){
		    @Override
		    public void run() {
		    	_stop();
		    }
		}).start();

	}
	
/*
	private void ping() {
		
	}


	class Ping extends TimerTask {
	    public void run() {
	    	
	       System.out.println("Hello World!"); 
	    }
	 }
*/

}
