import pprint
import json
import os
import re

this_dir, this_filename = os.path.split(__file__)

def getInner(json):
    key = next(iter(json.keys()))
    return (json[key])

def getScalars(json):
    processed_dict = {}
    for entry in (json).get('scalars'):
        processed_dict[entry["module"]] = entry["value"]
    return processed_dict

def getVectors(json):
    processed_dict = {}
    for entry in (json).get('vectors'):
        processed_dict[entry["module"]] = entry["value"]
    return processed_dict

def getSenderGSNum(module_name):
    sender_pattern = re.compile(r'SatSGP4Network.shell\[0\].groundStation\[(\d+)\]\.app\[(\d+)\]')
    sender_match = sender_pattern.search(module_name)
    if sender_match:
        sender_gs = sender_match.group(1)
        return int(sender_gs)
    else:
        return -1


def getReceiverGSNum(module_name):
    listener_pattern = re.compile(r'SatSGP4Network.shell\[0\].groundStation\[(\d+)\]\.app\[(\d+)\]')
    listener_match = listener_pattern.search(module_name)
    if listener_match:
        listener_gs = listener_match.group(1)
        return int(listener_gs)
    else:
        return -1

def make_mappings():
    # Path to the mappings.txt file
    

    mappings_file = os.path.join(this_dir, "mappings.txt")

    # Ensure the file exists
    try:
        with open(mappings_file, 'r') as file:
            lines = file.readlines()
    except FileNotFoundError:
        print(f"Error: File {mappings_file} not found!")
        exit(1)

    # Remove empty lines
    lines = [line.strip() for line in lines if line.strip()]

    # Regular expression patterns to capture relevant information
    sender_pattern = re.compile(r'SatSGP4Network.shell\[0\].groundStation\[(\d+)\]\.app\[(\d+)\]\.destAddress = "SatSGP4Network.shell\[0\].groundStation\[(\d+)\]"')
    port_pattern = re.compile(r'SatSGP4Network.shell\[0\].groundStation\[(\d+)\]\.app\[(\d+)\]\.destPort = (\d+)')
    listener_pattern = re.compile(r'SatSGP4Network.shell\[0\].groundStation\[(\d+)\]\.app\[(\d+)\]\.localPort = (\d+)')

    # Lists to store tuples
    senders = []
    listeners = []

    # Parse the lines for sender and listener information
    for i, line in enumerate(lines):
        # Match senders
        sender_match = sender_pattern.search(line)
        if sender_match:
            sender_gs = sender_match.group(1)
            sender_app = sender_match.group(2)
            if int(sender_app) % 2 != 0:
                print("Sender Apps wrong")
            dest_gs = sender_match.group(3)

            # Check for destination port in subsequent lines
            for j in range(i + 1, len(lines)):
                port_match = port_pattern.search(lines[j])
                if port_match:
                    dest_port = port_match.group(3)
                    senders.append((
                        f"SatSGP4Network.shell[0].groundStation[{sender_gs}].app[{sender_app}]",
                        f"SatSGP4Network.shell[0].groundStation[{dest_gs}] + port {dest_port}"
                    ))
                    break

        # Match listeners
        listener_match = listener_pattern.search(line)
        if listener_match:
            listener_gs = listener_match.group(1)
            listener_app = listener_match.group(2)
            if int(listener_app) % 2 == 0:
                print("listener Apps wrong")
            listener_port = listener_match.group(3)
            listeners.append((
                f"SatSGP4Network.shell[0].groundStation[{listener_gs}].app[{listener_app}]",
                f"SatSGP4Network.shell[0].groundStation[{listener_gs}] + port {listener_port}"
            ))

    # Combine the two lists into a final list of tuples
    # combined_list = list(zip(senders, listeners))
    combined_list = {}
    for sender in senders:
        for listener in listeners:
            if sender[1] == listener[1]:
                combined_list[sender[0]] = listener[0]

    # Output the combined list
    # print(f"Final Combined List ({len(combined_list)} elements):")
    # for item in combined_list:
    #     print(item)

    # Ensure the final list has the expected number of elements
    if len(combined_list) == 2070:
        print("The combined list has the expected 2070 elements.")
    else:
        print(f"Warning: The combined list has {len(combined_list)} elements, expected 2070.")
    return combined_list

def main():
    files = os.listdir(".")
    mappings = make_mappings()
    # json_count = sum(1 for file in files if file.endswith(".json"))
    # print(f"Number of JSON files in the current directory: {json_count}")

    base_names = ["BasicCrossTrafficVoIP"]
    fileNames = ["_packetSent.json", "_packetReceived.json", "_droppedPacketsQueueOverflow.json","_endToEndDelay.json"]
    # num_files = json_count
    # repetitions = int(num_files/len(base_names)/4) 
    repetitions = 5
    # requires experiments to be run with same # of repetitions
    jsons ={}
    packetsSent = {}
    packetsRecieved = {}
    packetsDropped = {}
    endToEndTimes = {}
    queueTimes = {}
    stat_dict = {}

    for i in range(len(base_names)):
        for j in range(repetitions):
            for k in range(len(fileNames)):
                fileName = base_names[i] + str(j) + fileNames[k]
                absolute_fileName = os.path.join(this_dir, fileName)
                try:
                    with open(absolute_fileName, 'r') as file:
                        jsons[fileName] = getInner(json.loads(file.read()))

                        if k == 0:
                            packetsSent[base_names[i] + str(j)] = getScalars((jsons[fileName]))
                        elif k == 1:
                            packetsRecieved[base_names[i] + str(j)] = getScalars((jsons[fileName]))
                        elif k == 2:
                            packetsDropped[base_names[i] + str(j)] = getScalars((jsons[fileName]))
                        elif k == 3:
                            endToEndTimes[base_names[i] + str(j)] = getVectors((jsons[fileName]))
                        else:
                            print("Error")
                except FileNotFoundError:
                    print(f"Error: File {fileName} not found!")
                    exit(1)
                    
    for base_name in base_names:
        for rep in range(repetitions):
            for sent in iter(packetsSent.get(base_name+str(rep)).keys()):
                if  packetsSent.get(base_name+str(rep))[sent] < packetsRecieved.get(base_name+str(rep))[mappings.get(sent)]:
                    print("Sent-recieved do not add up")
            
            foreground_packets_sent = sum((value for key, value in packetsSent.get(base_name+str(rep)).items() if getSenderGSNum(key) > 45))
            foreground_packets_recieved = sum((value for key, value in packetsRecieved.get(base_name+str(rep)).items() if getReceiverGSNum(key) > 45))
            all_packets_sent = sum((packetsSent.get(base_name+str(rep)).values()))
            all_packets_lost = sum((packetsDropped.get(base_name+str(rep)).values()))


            end_to_end_delay = 0
            for values in endToEndTimes.get(base_name+str(rep)).values():
                # average value of vector
                end_to_end_delay += sum(values)/len(values)
                end_to_end_delay = end_to_end_delay/len(endToEndTimes.get(base_name+str(rep)).values())
            # print(foreground_packets_recieved)
            # TODO sim-times
            loss_rate = all_packets_lost / all_packets_sent
            throughput = foreground_packets_recieved*1275/(600)
            # stat_dict[base_name]
            print(base_name+str(rep) + " stats:")
            print("\tthroughput: " + str(throughput) + "bps")
            print("\tend_to_end_delay: " + str(end_to_end_delay) + "s") # check the average by hand...?
            print("\tloss_rate: " + str(loss_rate) + "%")


if __name__ == "__main__":
    main()