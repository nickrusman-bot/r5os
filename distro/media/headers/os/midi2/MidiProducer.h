
#ifndef _MIDI_PRODUCER_H
#define _MIDI_PRODUCER_H

#include <List.h>
#include <Locker.h>
#include <MidiEndpoint.h>

namespace BPrivate { class BMidiRosterLooper; }

class BMidiProducer : public BMidiEndpoint
{
public:

	status_t Connect(BMidiConsumer *cons);
	status_t Disconnect(BMidiConsumer *cons);
	bool IsConnected(BMidiConsumer *cons) const;
	BList *Connections() const;

private:

	friend class BMidiLocalProducer;
	friend class BMidiRoster;
	friend class BPrivate::BMidiRosterLooper;
	
	BMidiProducer(const char *name = NULL);	
	virtual ~BMidiProducer();

	virtual void _Reserved1();
	virtual void _Reserved2();
	virtual void _Reserved3();
	virtual void _Reserved4();
	virtual void _Reserved5();
	virtual void _Reserved6();
	virtual void _Reserved7();
	virtual void _Reserved8();		

	status_t SendConnectRequest(BMidiConsumer *, bool);
	void ConnectionMade(BMidiConsumer *);
	bool ConnectionBroken(BMidiConsumer *);

	int32 CountConsumers() const;
	BMidiConsumer *ConsumerAt(int32) const;

	bool LockProducer() const;
	void UnlockProducer() const;

	BList *fConnections;
	mutable BLocker fLocker;
	
	uint32 _reserved[2];
};

class BMidiLocalProducer : public BMidiProducer
{
public:

	BMidiLocalProducer(const char *name = NULL);	
	
	virtual void Connected(BMidiConsumer *cons);
	virtual void Disconnected(BMidiConsumer *cons);
	
	void SprayData(
		void *data, size_t length, bool atomic = false, 
		bigtime_t time = 0) const;
	
	void SprayNoteOff(
		uchar channel, uchar note, uchar velocity, 
		bigtime_t time = 0) const;

	void SprayNoteOn(
		uchar channel, uchar note, uchar velocity, 
		bigtime_t time = 0) const;

	/* 16-bit velocity, as one MIDI 2.0 channel-voice packet. The MIDI 1.0
	 * SprayNoteOn above stays the only candidate for a bare integer.
	 */
	void SprayNoteOn2(
		uchar channel, uchar note, uint16 velocity,
		bigtime_t time = 0) const;

	void SprayKeyPressure(
		uchar channel, uchar note, uchar pressure, 
		bigtime_t time = 0) const;

	void SprayControlChange(
		uchar channel, uchar controlNumber, uchar controlValue, 
		bigtime_t time = 0) const;

	/* 32-bit controller value, as one MIDI 2.0 channel-voice packet.
	 * SprayControlChange above stays the MIDI 1.0 call.
	 */
	void SprayControlChange2(
		uchar channel, uchar controller, uint32 value,
		bigtime_t time = 0) const;

	/* One Universal MIDI Packet. wordCount is 1, 2, or 4 and must match
	 * the message type in ump[0].
	 */
	status_t SprayUMP(
		const uint32* ump, size_t wordCount, bigtime_t time = 0) const;

	void SprayProgramChange(
		uchar channel, uchar programNumber, bigtime_t time = 0) const;

	void SprayChannelPressure(
		uchar channel, uchar pressure, bigtime_t time = 0) const;

	void SprayPitchBend(
		uchar channel, uchar lsb, uchar msb, bigtime_t time = 0) const;

	void SpraySystemExclusive(
		void *data, size_t length, bigtime_t time = 0) const;

	void SpraySystemCommon(
		uchar status, uchar data1, uchar data2, bigtime_t time = 0) const;

	void SpraySystemRealTime(
		uchar status, bigtime_t time = 0) const; 

	void SprayTempoChange(
		int32 beatsPerMinute, bigtime_t time = 0) const;	
	
protected:

	~BMidiLocalProducer();
	
private:

	void SprayEvent(
		const void *data, size_t length, bool atomic, bigtime_t time,
		bool sysex = false) const;
	
	virtual void _Reserved1();
	virtual void _Reserved2();
	virtual void _Reserved3();
	virtual void _Reserved4();
	virtual void _Reserved5();
	virtual void _Reserved6();
	virtual void _Reserved7();
	virtual void _Reserved8();

	uint32 _reserved[2];		
};

#endif // _MIDI_PRODUCER_H
