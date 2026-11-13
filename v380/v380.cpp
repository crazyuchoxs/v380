// v380.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "UtlSocket.h"
#include "UtlDiscovery.h"
#include "aes.h"
#include "FlvStream.h"

#pragma pack (push, 1)
struct TCommandReq {
	int32_t command;   // NEW_USERVERIFY = 1167
	union {
		struct {
			uint32_t unknown1; // 120 = login LAN (user@49, pw@81); 1022 = layout cloud/domain (user@71, pw@103) -> 1011
			uint8_t unknown2;  // versi protokol: firmware ini menerima >=31 (2 -> balasan -100)
			uint32_t unknown3; // 10 = LoginFromServerEX, 11 = LoginFromMRServerEX, 1=our
			uint32_t deviceId;
			char hostDateTime[32]; // optional - maybe related to unk1/2, on v380 pc this is a domain name
			char username[32];
			char password[32]; // consists of randomkey:password
		} login;
		struct {
			uint32_t deviceId;   // 4:  +292
			uint32_t unknown1;   // 8:  +284    = HSPI_V_StartPreview a4
			uint16_t requestFps; // 12: FPS, min=10, max=20 (default 20, invalid value will fall back to 13)
			uint32_t authTicket; // 14: +304
			uint32_t unknown3;   // 18: unused
			uint32_t unknown4;   // 22: *(+344) == !0 + 4096 (v14 + 4096) "0x1001" audio related?
			uint32_t resolution; // 26: +288    = 0 = LowRes, 1 = HiRes
			uint32_t unknown6;   // 30: unused
		} streamLogin_lan;
		struct {
			uint32_t unknown1; // 1022
			char domain[32];
			uint8_t unknown2[18];
			uint16_t camPort;    // 58: v15 + 180 = 0x60 0x22 0x31 0x37 "8800"
			uint16_t unknown3;   // 60: This is not used (corrupted overlapped with domain)
			uint32_t deviceId;   // 62: v51
			uint32_t authTicket; // 66: v49
			uint32_t session;    // 70: v52
			uint32_t unknown4;   // 74: v57 = 0
			uint8_t unknown5;    // 78: = 20 hardcoded
			uint32_t unknown6;   // 79: v14 = 0x1001
			uint32_t unknown7;   // 83: v56 = 0
		} streamLogin_cloud;
		struct {
			uint32_t unknown1;
		} streamStart;
	} u;
};

struct TLoginResp {
	int32_t command;
	int32_t loginResult;
	int32_t resultValue;
	uint8_t version;
	uint32_t authTicket;
	uint32_t session;
	uint8_t deviceType;
	uint8_t camType;
	uint16_t vendorId;
	uint16_t isDomainExists;
	char domain[32];
	int32_t recDevId;
	uint8_t nChannels;
	uint8_t nAudioPri;
	uint8_t nVideoPri;
	uint8_t nSpeaker;
	uint8_t nPtzPri;
	uint8_t nReversePri;
	uint8_t nPtzXPri;
	uint8_t nPtzXCount;
	char settings[32];
	uint16_t panoX;
	uint16_t panoY;
	uint16_t panoRad; // 107
	uint32_t unknown1;
	uint8_t canUpdateDevice; // 112 (canUpdateDevice = [112] == 2)
};

struct TStreamLogin301 {
	int32_t command; // 401
	int32_t v21;
	uint16_t fps;
	uint32_t width;
	uint32_t height;
};
#pragma pack (pop)

const uint8_t cam_select[] = { 0x01, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t ptz_right[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe9, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_left[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xea, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_up[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xeb, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_down[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xec, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t send_cmd[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t light_on[] = { 0xc4, 0x00, 0x00, 0x00, 0xe9, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t light_off[] = { 0xc4, 0x00, 0x00, 0x00, 0xea, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t light_auto[] = { 0xc4, 0x00, 0x00, 0x00, 0xeb, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

bool readKey(bool& up, bool& down, bool& left, bool& right, int& light);


std::string generateRandomPrintable(size_t len)
{
	char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*()_+-=";
	int nSet = sizeof(set);

	std::string s;
	s.resize(len);

	std::unique_ptr<char> c(new char);
	srand((unsigned int)(size_t)c.get());

	for (size_t i = 0; i < len; i++) {
		s[i] = set[rand() % nSet];
	}

	return s;
}

void GeneratePassword(std::vector<uint8_t>& output, const std::string& password)
{
	const size_t nRandomKey = 16;
	std::string randomKey = generateRandomPrintable(nRandomKey);
	const char* staticKey = "macrovideo+*#!^@";

	std::vector<uint8_t> paddedPassword;
	size_t pad = AES_BLOCKLEN - (password.size() % AES_BLOCKLEN);
	paddedPassword.resize(password.size() + pad);
	memcpy_s(paddedPassword.data(), paddedPassword.size(), password.c_str(), password.size());

	AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t*)staticKey);

	size_t nBlock = paddedPassword.size() / AES_BLOCKLEN;
	for (size_t i = 0; i < nBlock; i++) {
		AES_ECB_encrypt(&ctx, paddedPassword.data() + i * AES_BLOCKLEN);
	}

	AES_ctx ctx2;
	AES_init_ctx(&ctx2, (uint8_t*)randomKey.data());
	for (size_t i = 0; i < nBlock; i++) {
		AES_ECB_encrypt(&ctx2, paddedPassword.data() + i * AES_BLOCKLEN);
	}

	output.resize(nRandomKey + nBlock * AES_BLOCKLEN);
	memcpy_s(output.data(), output.size(), randomKey.c_str(), nRandomKey);
	memcpy_s(output.data() + nRandomKey, output.size() - nRandomKey, paddedPassword.data(), paddedPassword.size());
}

// Dekripsi payload video untuk deviceVersion > 30:
// 64 dari setiap 80 byte pertama di-AES-ECB-decrypt (sisa 16 byte tidak dienkripsi)
void DecryptVideoPayload(uint8_t* data, size_t len, const uint8_t* key)
{
	AES_ctx ctx;
	AES_init_ctx(&ctx, key);
	for (size_t off = 0; off + 64 <= len; off += 80) {
		for (int i = 0; i < 4; i++) {
			AES_ECB_decrypt(&ctx, data + off + i * AES_BLOCKLEN);
		}
	}
}

// aesKey media = ticket(u32 LE) + 0x618123462c14795c(u64 LE) + 0x82800df0(u32 LE)
void GenerateMediaKey(uint8_t key[16], uint32_t ticket)
{
	key[0] = (uint8_t)(ticket & 0xff);
	key[1] = (uint8_t)((ticket >> 8) & 0xff);
	key[2] = (uint8_t)((ticket >> 16) & 0xff);
	key[3] = (uint8_t)((ticket >> 24) & 0xff);
	const uint64_t mid = 0x618123462c14795cULL;
	for (int i = 0; i < 8; i++) key[4 + i] = (uint8_t)((mid >> (8 * i)) & 0xff);
	const uint32_t tail = 0x82800df0;
	key[12] = (uint8_t)(tail & 0xff);
	key[13] = (uint8_t)((tail >> 8) & 0xff);
	key[14] = (uint8_t)((tail >> 16) & 0xff);
	key[15] = (uint8_t)((tail >> 24) & 0xff);
}

// Siapkan satu frame video:
//  - out   : Annex-B murni (00 00 00 01 ...) untuk stdout --output=video
//  - flvOut: header timestamp 16 byte + payload (didekripsi bila perlu) untuk FLV
void PrepareVideoFrame(const std::vector<uint8_t>& frame, bool needDecrypt, const uint8_t* key,
	std::vector<uint8_t>& out, std::vector<uint8_t>& flvOut)
{
	out.clear();
	flvOut.clear();
	if (frame.size() <= 16) return;

	// header luar: frameId:4, frameType:2, frameRate:2, timestamp:8
	std::vector<uint8_t> payload(frame.begin() + 16, frame.end());
	if (needDecrypt) {
		DecryptVideoPayload(payload.data(), payload.size(), key);
	}

	flvOut.assign(frame.begin(), frame.begin() + 16);
	flvOut.insert(flvOut.end(), payload.begin(), payload.end());

	out.swap(payload);
	size_t s = std::string::npos;
	for (size_t i = 0; i + 2 < out.size(); i++) {
		if (out[i] == 0 && out[i + 1] == 0 && out[i + 2] == 1) { s = i; break; }
	}
	if (s != std::string::npos) {
		std::vector<uint8_t> tmp;
		tmp.reserve(out.size() - s + 1);
		tmp.push_back(0);
		tmp.insert(tmp.end(), out.begin() + s, out.end());
		out.swap(tmp);
	}
}

// Panjang respons yang dikirim kamera (diukur dari firmware ini)
const size_t kAuthRespLen = 256;        // respons login 1168
const size_t kStreamLoginRespLen = 32;  // respons stream login 401

void printHelp(FILE* f)
{
	fprintf(f, "Usage example:\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -addr 192.168.1.2\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -mac aa:bb:cc:11:22:33\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -id 123456789\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -id 123456789 -addr 192.168.1.2\n");
	fprintf(f, "  v380 -p password -addr 192.168.1.2 | ffplay -vcodec h264 -probesize 32 -formatprobesize 0 -avioflags direct -flags low_delay -i -\n");
	fprintf(f, "\n");
	fprintf(f, "OPTIONS:\n");
	fprintf(f, "  -u              username         (default admin)\n");
	fprintf(f, "  -p              password\n");
	fprintf(f, "  -addr           camera IP/address\n");
	fprintf(f, "  -mac            camera MAC address\n");
	fprintf(f, "  -id             camera ID\n");
	fprintf(f, "  -port           camera port      (default 8800)\n");
	fprintf(f, "  -retry          Number of connection attempt (default 5)\n");
	fprintf(f, "  --light=        Initial light (0=off, 1=on, 2=auto)\n");
	fprintf(f, "  --enable-ptz=0  Disable pan-tilt-zoom via keyboard press\n");
	fprintf(f, "  --low-res       Use low resolution\n");
	fprintf(f, "  --discover      Discover camera\n");
	fprintf(f, "  --output=video  Output a video\n");
	fprintf(f, "  --output=audio  Output an audio (ADPCM 8000hz)\n");
	fprintf(f, "  --output=flv    Uses flv as output, experimental\n");
	fprintf(f, "  --no-video      FLV: disable video output\n");
	fprintf(f, "  --no-audio      FLT: disable audio output\n");
	fprintf(f, "  -h              Show this help\n");
}

int main(int argc, const char* argv[])
{
	int retry = 0;

	std::string ip = "";
	std::string id = "";
	std::string mac = "";
	std::string port = "8800";
	std::string username = "admin";
	std::string password = "password";
	int resolution = 1;
	int light = -1;
	bool enable_ptz = true;
	bool show_help = false;
	int output_type = 0; // 0 = video, 1 = audio, 2 = flv
	int retryCount = 5;
	bool enable_video = true;
	bool enable_audio = true;

	for (int i = 0; i < argc; i++)
	{
		if ((_stricmp(argv[i], "-u") == 0) && ((i + 1) < argc))
		{
			username = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-p") == 0) && ((i + 1) < argc))
		{
			password = argv[i + 1];
		}
		if ((_stricmp(argv[i], "-retry") == 0) && ((i + 1) < argc))
		{
			retryCount = atoi(argv[i + 1]);
		}
		else if ((_stricmp(argv[i], "-mac") == 0) && ((i + 1) < argc))
		{
			mac = argv[i + 1];
			auto asciitolower = [](char in)->char {
				if (in <= 'Z' && in >= 'A')
					return in - ('Z' - 'z');
				return in;
			};

			std::transform(mac.begin(), mac.end(), mac.begin(), asciitolower);
		}
		else if ((_stricmp(argv[i], "-id") == 0) && ((i + 1) < argc))
		{
			id = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-addr") == 0) && ((i + 1) < argc))
		{
			ip = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-port") == 0) && ((i + 1) < argc))
		{
			port = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "--enable-ptz=0") == 0))
		{
			enable_ptz = false;
		}
		else if ((_stricmp(argv[i], "--low-res") == 0))
		{
			resolution = 0;
		}
		else if ((_stricmp(argv[i], "--light=0") == 0))
		{
			light = 0;
		}
		else if ((_stricmp(argv[i], "--light=1") == 0))
		{
			light = 1;
		}
		else if ((_stricmp(argv[i], "--light=2") == 0))
		{
			light = 2;
		}
		else if ((_stricmp(argv[i], "--output=video") == 0))
		{
			output_type = 0;
		}
		else if ((_stricmp(argv[i], "--output=audio") == 0))
		{
			output_type = 1;
		}
		else if ((_stricmp(argv[i], "--output=flv") == 0))
		{
			output_type = 2;
		}
		else if ((_stricmp(argv[i], "--no-audio") == 0))
		{
			enable_audio = false;
		}
		else if ((_stricmp(argv[i], "--no-video") == 0))
		{
			enable_video = false;
		}
		else if ((_stricmp(argv[i], "-h") == 0) || (_stricmp(argv[i], "--help") == 0))
		{
			show_help = true;
		}
		else if ((_stricmp(argv[i], "-d") == 0) || (_stricmp(argv[i], "--discover") == 0))
		{
			UtlDiscovery socketDiscovery;
			auto vDevices = socketDiscovery.Discover();

			for (auto it = vDevices.begin(); it != vDevices.end(); ++it) {
				printf(" ID:  %s\n IP:  %s\n MAC: %s\n\n", it->devid.c_str(), it->ip.c_str(), it->mac.c_str());
			}
			return 0;
		}
	}

	if (ip.empty() && mac.empty() && id.empty()) {
		fprintf(stderr, "Camera address not set\n\n");
		printHelp(stderr);
		return 1;
	}

	if (port.empty()) {
		fprintf(stderr, "Camera port not set\n\n");
		printHelp(stderr);
		return 1;
	}

	if (show_help) {
		printHelp(stdout);
		return 0;
	}

	std::vector<uint8_t> ptzcmd(send_cmd, send_cmd + sizeof(send_cmd));

#ifdef _WIN32
	_setmode(_fileno(stdout), O_BINARY);
#endif

	FlvStream flv;

	while (retry++ < retryCount || retryCount == 0)
	{
		try
		{
			UtlSocket socketAuth;
			UtlSocket socketStream;

			std::vector<uint8_t> buf;
			std::vector<uint8_t> hdr;
			std::vector<uint8_t> vframe;
			std::vector<uint8_t> aframe;
			std::vector<uint8_t> nframe;
			std::vector<uint8_t> fframe;
			std::vector<uint8_t> rbuf(512, 0); // buffer khusus respons (mencegah req jadi dangling)

			buf.reserve(500);
			hdr.reserve(12);
			vframe.reserve(8192);
			aframe.reserve(8192);
			nframe.reserve(8192);
			fframe.reserve(8192);

			{
				UtlDiscovery socketDiscovery;
				auto vDevices = socketDiscovery.Discover();

				for (auto it = vDevices.begin(); it != vDevices.end(); ++it) {
					if (ip.size() && ip == it->ip) {
						id = it->devid;
						break;
					}
					if (id.size() && id == it->devid) {
						ip = it->ip;
						break;
					}
					if (mac.size() && mac == it->mac) {
						ip = it->ip;
						id = it->devid;
						break;
					}
				}

				if (ip.empty()) {
					fprintf(stderr, "Unable to find camera with specified mac/id\n");
					continue;
				}
			}
			
			socketAuth.Connect(ip, port);

			std::vector<uint8_t> pw;
			GeneratePassword(pw, password);

			buf.resize(256, 0);
			TCommandReq* req = reinterpret_cast<TCommandReq*>(buf.data());
			TLoginResp resp;

			req->command = 1167;
			req->u.login.deviceId = stoi(id);
			req->u.login.unknown1 = 120;
			req->u.login.unknown2 = 31;
			req->u.login.unknown3 = 1;
			memcpy_s(req->u.login.username, 32, username.c_str(), username.size());
			memcpy_s(req->u.login.password, 32, pw.data(), pw.size());

			socketAuth.Send(buf);
			int nAuth = 0;
			while (nAuth < (int)kAuthRespLen) {
				int r = socketAuth.Recv(rbuf.data() + nAuth, kAuthRespLen - nAuth, 5000);
				if (r <= 0) break;
				nAuth += r;
			}
			socketAuth.Close();

			if (nAuth < (int)sizeof(TLoginResp)) {
				fprintf(stderr, "Login response: expected >= %d bytes, got %d\n", (int)sizeof(TLoginResp), nAuth);
				continue;
			}
			resp = *reinterpret_cast<TLoginResp*>(rbuf.data());
			if (resp.command == 1168) {
				if (resp.loginResult == 1001) {
					//fprintf(stderr, "%x", resp->resultValue);
				} else if (resp.loginResult == 1011) {
					fprintf(stderr, "Invalid username\n");
					return resp.loginResult;
				} else if (resp.loginResult == 1012) {
					fprintf(stderr, "Invalid password\n");
					return resp.loginResult;
				} else if (resp.loginResult == 1018) {
					fprintf(stderr, "Invalid device id\n");
					return resp.loginResult;
				}
			}

			if (resp.command != 1168 || resp.loginResult != 1001) {
				fprintf(stderr, "Login gagal (cmd=%d result=%d)\n", resp.command, resp.loginResult);
				continue;
			}

			// deviceVersion > 30 -> stream media terenkripsi AES-128-ECB
			bool needDecrypt = resp.version > 30;
			uint8_t mediaKey[16] = { 0 };
			if (needDecrypt) GenerateMediaKey(mediaKey, resp.authTicket);

			socketStream.Connect(ip, port);
			socketStream.DisableNagle();

			std::fill(buf.begin(), buf.end(), 0);
			req->command = 301; // stream login
			req->u.streamLogin_lan.deviceId = stoi(id);
			req->u.streamLogin_lan.unknown1 = 0;
			req->u.streamLogin_lan.requestFps = 20;
			req->u.streamLogin_lan.authTicket = resp.authTicket;
			req->u.streamLogin_lan.unknown4 = 4096 + 1; // not sure
			req->u.streamLogin_lan.resolution = resolution;

			socketStream.Send(buf);
			int nStream = 0;
			while (nStream < (int)kStreamLoginRespLen) {
				int r = socketStream.Recv(rbuf.data() + nStream, kStreamLoginRespLen - nStream, 5000);
				if (r <= 0) break;
				nStream += r;
			}

			if (nStream < (int)sizeof(TStreamLogin301)) {
				fprintf(stderr, "Login response: expected >= %d bytes, got %d\n", (int)sizeof(TStreamLogin301), nStream);
				continue;
			}

			TStreamLogin301 loginResp = *reinterpret_cast<TStreamLogin301*>(rbuf.data());
			if (loginResp.command != 401) {
				fprintf(stderr, "Login response: expected 401, got %d\n", loginResp.command);
				continue;
			}

			if (loginResp.v21 == -11 || loginResp.v21 == -12) {
				fprintf(stderr, "Login response: unsupported %d, continuing\n", loginResp.v21);
			} else if (loginResp.v21 != 402 && loginResp.v21 != 1001) {
				fprintf(stderr, "Login response: unsupported %d\n", loginResp.v21);
				continue;
			}

			req->command = 303; // stream start
			req->u.streamStart.unknown1 = loginResp.v21;
			socketStream.Send(buf);

			buf.clear();

			if (output_type == 2) {
				flv.Init(enable_video, enable_audio);
			}

			bool exitloop = false;
			int hdrn = 0;
			hdr.resize(12);
			while (!exitloop)
			{
				// TCP bisa memecah header 12 byte jadi beberapa segmen - akumulasi dulu
				hdrn = 0;
				while (hdrn < 12) {
					int r = socketStream.Recv(hdr.data() + hdrn, 12 - hdrn, 5000);
					if (r <= 0) break;
					hdrn += r;
				}
				if (hdrn < 12) break;
				switch (hdr[0])
				{
				case 0x7f:
				{
					uint16_t totalFrame = *(uint16_t*)&hdr[3];
					uint16_t curFrame = *(uint16_t*)&hdr[5];
					uint16_t len = *(uint16_t*)&hdr[7];
					uint8_t type = hdr[1];

					if (len > 20000 || !totalFrame || curFrame >= totalFrame) {
						fprintf(stderr, "Sanity check failed (total=%u cur=%u len=%u), restarting\n", totalFrame, curFrame, len);
						exitloop = true;
						break;
					}

					buf.resize(len);
					int n = 0;
					while (n < len) {
						n += socketStream.Recv(buf.data() + n, len - n, 5000);
					}

					retry = 0;

					switch (type)
					{
					case 0x00: // I-Frame
						vframe.insert(vframe.end(), buf.begin(), buf.end());
						if (curFrame == totalFrame - 1) {
							PrepareVideoFrame(vframe, needDecrypt, mediaKey, nframe, fframe);
							if (output_type == 0) {
								fwrite(nframe.data(), 1, nframe.size(), stdout);
								fflush(stdout);
							} else if (output_type == 2) {
								flv.WriteVideo(fframe, type == 0x00);
							}
							vframe.clear();
						}
						break;

					case 0x01: // P-Frame
						// Video
						vframe.insert(vframe.end(), buf.begin(), buf.end());
						if (curFrame == totalFrame - 1) {
							PrepareVideoFrame(vframe, needDecrypt, mediaKey, nframe, fframe);
							if (output_type == 0) {
								fwrite(nframe.data(), 1, nframe.size(), stdout);
								fflush(stdout);
							} else if (output_type == 2) {
								flv.WriteVideo(fframe, type == 0x00);
							}
							vframe.clear();
						}
						break;

					case 0x16:
						// Audio
						// sox -t ima -r 8000 -e ms-adpcm streamfile.raw -e signed-integer -b 16 out.wav
						// ffmpeg -f s16le -ar 8000 -ac 1 -acodec adpcm_ima_ws streamfile.raw out.wav

						aframe.insert(aframe.end(), buf.begin(), buf.end());
						if (curFrame == totalFrame - 1) {
							if (output_type == 0) {
								fwrite(aframe.data() + 20, 1, aframe.size() - 20, stdout);
								fflush(stdout);
							} else if (output_type == 2) {
								flv.WriteAudio(aframe);
							}
							aframe.clear();
						}
						break;
					}

					break;
				}
				case 0x1f:
					// Not sure what this does yet
					fprintf(stderr, "Unparsed 0x1f data\n");
					break;
				case 0x6f:
					Sleep(20);
					break;
				default:
					exitloop = true;
					fprintf(stderr, "Unknown 0x%02x command, header:", hdr[0]);
					for (int i = 0; i < 12; i++) fprintf(stderr, " %02x", hdr[i]);
					fprintf(stderr, "\n");
					break;
				}
				
				if (light != -1) {
					// Set initial light and from readKey
					if (light == 0) {
						socketStream.Send(light_off, sizeof(light_off));
					} else if (light == 1) {
						socketStream.Send(light_on, sizeof(light_on));
					} else if (light == 2) {
						socketStream.Send(light_auto, sizeof(light_auto));
					}
					light = -1;
				}

				bool up, dn, l, r;
				if (enable_ptz && readKey(up, dn, l, r, light)) {
					ptzcmd[8] = 0xe8;
					ptzcmd[10] = 0xe8;
					if (up) ptzcmd[10] = 0xeb;
					if (dn) ptzcmd[10] = 0xec;
					if (l) ptzcmd[8] = 0xea;
					if (r) ptzcmd[8] = 0xe9;
					
					socketStream.Send(ptzcmd);
				}
			}
			
			fprintf(stderr, "Stream stopped, restarting stream\n");
		}
		catch (const std::exception& ex)
		{
			fprintf(stderr, "%s\n", ex.what());
		}
	}

    return 0;
}

bool readKey(bool& up, bool& down, bool& left, bool& right, int& light)
{
	bool ret = false;

	up = false;
	down = false;
	left = false;
	right = false;
	light = -1;

#ifdef _WIN32
	if (GetForegroundWindow() == GetConsoleWindow())
	{
		if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0)
		{
			right = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0)
		{
			left = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0)
		{
			up = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0)
		{
			down = true;
			ret = true;
		}
		if ((GetAsyncKeyState('I') & 0x8000) != 0)
		{
			light = 1;
			ret = true;
		}
		if ((GetAsyncKeyState('O') & 0x8000) != 0)
		{
			light = 0;
			ret = true;
		}
		if ((GetAsyncKeyState('P') & 0x8000) != 0)
		{
			light = 2;
			ret = true;
		}
	}
#endif

	return ret;
}