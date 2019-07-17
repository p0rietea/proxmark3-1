//-----------------------------------------------------------------------------
// Craig Young, 2014
// Christian Herrmann, 2017
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// main code for HF standalone mode Mifare /sniff/emulation by Craig Young
//-----------------------------------------------------------------------------

#include "hf_young.h"
#include "common.h"

typedef struct {
    uint8_t uid[10];
    uint8_t uidlen;
    uint8_t atqa[2];
    uint8_t sak;
} PACKED card_clone_t;


void ModInfo(void) {
    DbpString("  HF Mifare sniff/simulation - (Craig Young)");
}

void RunMod() {
    StandAloneMode();
    Dbprintf(">>  Craig Young Mifare sniff UID/clone uid 2 magic/sim  a.k.a YoungRun Started  <<");
    FpgaDownloadAndGo(FPGA_BITSTREAM_HF);

    int selected = 0, playing = 0, iGotoRecord = 0, iGotoClone = 0;
    int cardRead[OPTS] = {0};

    card_clone_t uids[OPTS];
    iso14a_card_select_t card[OPTS];
    uint8_t params = (MAGIC_SINGLE | MAGIC_DATAIN);

    LED(selected + 1, 0);

    for (;;) {
        WDT_HIT();
        // exit from Standalone Mode,   send a usbcommand.
        if (data_available()) return;

        SpinDelay(300);

        if (iGotoRecord == 1 || cardRead[selected] == 0) {
            iGotoRecord = 0;
            LEDsoff();
            LED(selected + 1, 0);
            LED(LED_D, 0);

            // record
            Dbprintf("Enabling iso14443a reader mode for [Bank: %d]...", selected);
            /* need this delay to prevent catching some weird data */
            SpinDelay(500);
            iso14443a_setup(FPGA_HF_ISO14443A_READER_MOD);

            for (;;) {
                // exit from Standalone Mode,   send a usbcommand.
                if (data_available()) return;

                if (BUTTON_PRESS()) {
                    if (cardRead[selected]) {
                        Dbprintf("Button press detected -- replaying card in bank[%d]", selected);
                        break;
                    } else if (cardRead[(selected + 1) % OPTS]) {
                        Dbprintf("Button press detected but no card in bank[%d] so playing from bank[%d]", selected, (selected + 1) % OPTS);
                        selected = (selected + 1) % OPTS;
                        break; // playing = 1;
                    } else {
                        Dbprintf("Button press detected but no stored tag to play. (Ignoring button)");
                        SpinDelay(300);
                    }
                }

                if (!iso14443a_select_card(NULL, &card[selected], NULL, true, 0, true)) {
                    continue;
                } else {
                    Dbprintf("Read UID:");
                    Dbhexdump(card[selected].uidlen, card[selected].uid, 0);

                    if (memcmp(uids[(selected + 1) % OPTS].uid, card[selected].uid, card[selected].uidlen) == 0) {
                        Dbprintf("Card selected has same UID as what is stored in the other bank. Skipping.");
                    } else {
                        uids[selected].sak = card[selected].sak;
                        uids[selected].uidlen = card[selected].uidlen;
                        memcpy(uids[selected].uid, card[selected].uid, uids[selected].uidlen);
                        memcpy(uids[selected].atqa, card[selected].atqa, 2);

                        if (uids[selected].uidlen > 4)
                            Dbprintf("Bank[%d] received a 7-byte UID", selected);
                        else
                            Dbprintf("Bank[%d] received a 4-byte UID", selected);
                        break;
                    }
                }
            }

            Dbprintf("ATQA = %02X%02X", uids[selected].atqa[0], uids[selected].atqa[1]);
            Dbprintf("SAK = %02X", uids[selected].sak);
            LEDsoff();
            LED(LED_B,  200);
            LED(LED_A, 200);
            LED(LED_B,  200);
            LED(LED_A, 200);

            LEDsoff();
            LED(selected + 1, 0);

            // Next state is replay:
            playing = 1;

            cardRead[selected] = 1;
        }

        /* MF Classic UID clone */
        else if (iGotoClone == 1) {
            iGotoClone = 0;
            LEDsoff();
            LED(selected + 1, 0);
            LED(LED_A, 250);

            // magiccards holds 4bytes uid.  *usually*
            uint32_t tmpuid = bytes_to_num(uids[selected].uid, 4);

            // record
            Dbprintf("Preparing to Clone card [Bank: %d]; uid: %08x", selected, tmpuid);

            // wait for button to be released
            // Delay cloning until card is in place
            while (BUTTON_PRESS())
                WDT_HIT();

            Dbprintf("Starting clone. [Bank: %d]", selected);
            // need this delay to prevent catching some weird data
            SpinDelay(500);
            // Begin clone function here:
            /* Example from client/mifarehost.c for commanding a block write for "magic Chinese" cards:
                    SendCommandOLD(CMD_MIFARE_CSETBLOCK, params & (0xFE | (uid == NULL ? 0:1)), blockNo, 0, data, 16);

                Block read is similar:
                    SendCommandOLD(CMD_MIFARE_CGETBLOCK, params, blockNo, 0,...};
                We need to imitate that call with blockNo 0 to set a uid.

                The get and set commands are handled in this file:
                    // Work with "magic Chinese" card
                    case CMD_MIFARE_CSETBLOCK:
                            MifareCSetBlock(c->arg[0], c->arg[1], c->d.asBytes);
                            break;
                    case CMD_MIFARE_CGETBLOCK:
                            MifareCGetBlock(c->arg[0], c->arg[1], c->d.asBytes);
                            break;

                mfCSetUID provides example logic for UID set workflow:
                    -Read block0 from card in field with MifareCGetBlock()
                    -Configure new values without replacing reserved bytes
                            memcpy(block0, uid, 4); // Copy UID bytes from byte array
                            // Mifare UID BCC
                            block0[4] = block0[0]^block0[1]^block0[2]^block0[3]; // BCC on byte 5
                            Bytes 5-7 are reserved SAK and ATQA for mifare classic
                    -Use mfCSetBlock(0, block0, oldUID, wantWipe, MAGIC_SINGLE) to write it
            */
            uint8_t oldBlock0[16] = {0}, newBlock0[16] = {0};
            // arg0 = Flags, arg1=blockNo
            MifareCGetBlock(params, 0, oldBlock0);
            if (oldBlock0[0] == 0 && oldBlock0[0] == oldBlock0[1]  && oldBlock0[1] == oldBlock0[2] && oldBlock0[2] == oldBlock0[3]) {
                Dbprintf("No changeable tag detected. Returning to replay mode for bank[%d]", selected);
                playing = 1;
            } else {
                uint8_t testBlock0[16] = {0};
                Dbprintf("UID from target tag: %02X%02X%02X%02X", oldBlock0[0], oldBlock0[1], oldBlock0[2], oldBlock0[3]);
                memcpy(newBlock0 + 5, oldBlock0 + 5, 11);

                // Copy uid for bank (2nd is for longer UIDs not supported if classic)
                memcpy(newBlock0, uids[selected].uid, 4);
                newBlock0[4] = newBlock0[0] ^ newBlock0[1] ^ newBlock0[2] ^ newBlock0[3];

                // arg0 = workFlags, arg1 = blockNo, datain
                MifareCSetBlock(params, 0, newBlock0);
                MifareCGetBlock(params, 0, testBlock0);

                if (memcmp(testBlock0, newBlock0, 16) == 0) {
                    DbpString("Cloned successfull!");
                    cardRead[selected] = 0; // Only if the card was cloned successfully should we clear it
                    playing = 0;
                    iGotoRecord = 1;
                    selected = (selected + 1) % OPTS;
                } else {
                    Dbprintf("Clone failed. Back to replay mode on bank[%d]", selected);
                    playing = 1;
                }
            }
            LEDsoff();
            LED(selected + 1, 0);
        }

        // Change where to record (or begin playing)
        // button_pressed == BUTTON_SINGLE_CLICK && cardRead[selected])
        else if (playing == 1) {
            LEDsoff();
            LED(selected + 1, 0);

            // Begin transmitting
            LED(LED_B, 0);
            DbpString("Playing");
            for (; ;) {
                // exit from Standalone Mode,   send a usbcommand.
                if (data_available()) return;

                int button_action = BUTTON_HELD(1000);
                if (button_action == 0) {  // No button action, proceed with sim

                    uint8_t flags = FLAG_4B_UID_IN_DATA;
                    uint8_t data[PM3_CMD_DATA_SIZE] = {0}; // in case there is a read command received we shouldn't break

                    memcpy(data, uids[selected].uid, uids[selected].uidlen);

                    uint64_t tmpuid = bytes_to_num(uids[selected].uid, uids[selected].uidlen);

                    if (uids[selected].uidlen == 7) {
                        flags = FLAG_7B_UID_IN_DATA;
                        Dbprintf("Simulating ISO14443a tag with uid: %014" PRIx64 " [Bank: %d]", tmpuid, selected);
                    } else {
                        Dbprintf("Simulating ISO14443a tag with uid: %08" PRIx64 " [Bank: %d]", tmpuid, selected);
                    }

                    if (uids[selected].sak == 0x08 && uids[selected].atqa[0] == 0x04 && uids[selected].atqa[1] == 0) {
                        DbpString("Mifare Classic 1k");
                        SimulateIso14443aTag(1, flags, data);
                    } else if (uids[selected].sak == 0x18 && uids[selected].atqa[0] == 0x02 && uids[selected].atqa[1] == 0) {
                        DbpString("Mifare Classic 4k (4b uid)");
                        SimulateIso14443aTag(8, flags, data);
                    } else if (uids[selected].sak == 0x08 && uids[selected].atqa[0] == 0x44 && uids[selected].atqa[1] == 0) {
                        DbpString("Mifare Classic 4k (7b uid)");
                        SimulateIso14443aTag(8, flags, data);
                    } else if (uids[selected].sak == 0x00 && uids[selected].atqa[0] == 0x44 && uids[selected].atqa[1] == 0) {
                        DbpString("Mifare Ultralight");
                        SimulateIso14443aTag(2, flags, data);
                    } else if (uids[selected].sak == 0x20 && uids[selected].atqa[0] == 0x04 && uids[selected].atqa[1] == 0x03) {
                        DbpString("Mifare DESFire");
                        SimulateIso14443aTag(3, flags, data);
                    } else {
                        Dbprintf("Unrecognized tag type -- defaulting to Mifare Classic emulation");
                        SimulateIso14443aTag(1, flags, data);
                    }

                } else if (button_action == BUTTON_SINGLE_CLICK) {
                    selected = (selected + 1) % OPTS;
                    Dbprintf("Done playing. Switching to record mode on bank %d", selected);
                    iGotoRecord = 1;
                    break;
                } else if (button_action == BUTTON_HOLD) {
                    Dbprintf("Playtime over. Begin cloning...");
                    iGotoClone = 1;
                    break;
                }
            }

            /* We pressed a button so ignore it here with a delay */
            SpinDelay(300);
            LEDsoff();
            LED(selected + 1, 0);
        }
    }
}
