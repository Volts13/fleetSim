**Language & I/O Handling**   
I chose to use c++ for two main reasons. First, it is the language I am most comfortable in as classes in the University of Michigan are primarily done with c++. More importantly however, the precision I get with the lower level of abstraction along with it being an object oriented language makes it much smoother for me to tweak standard containers and create objects and structs/classes for things within the fleet simulation. To handle my outputs I chose to use JSON.hpp in order to simplify my code and make outputting states easier. In my main I use the while loop to parse through the JSON and send it back to the engine to handle the requests, which then send their own responses back through by returning a JSON object that main writes into stdout.

I am using ordered\_json to maintain the order that I create the message in. In addition, initialization order allows me to maintain the vessel order we were initially given like in the spec instead of leaving it up to the compiler, allowing me to print it out in order but keep the benefits of a hashmap under the hood. 

init() looks for “cargo\_manifest” when deciding to include new cargo within the simulation as an option. 

tick()’s parameters allow us to accept an optional manifest that adds cargo to the simulation.

get\_state\_at() utilizes a vector that logs the world-state payloads as we advance so we can find and call the previous one easily. A tradeoff by holding complete snapshots however is that as the number of ticks grows the memory grows with it.  
**Vessels**  
The vessel struct holds the id, location and current state. I chose to add an arrivalTime variable that is primarily used for my transit min\_heap mentioned later so I can easily see which vessels are close to or have arrived. 

The planned vessel helper is used so that changes made between ticks are maintained, such as when you first load it up with cargo then undock it. If we pull from the original “vessels” hashmap their state may not update properly if they have prior pending work loaded in pendingVessels.

Committed and planned states are designed so that many functions work when planning, but as soon as the tick activates and they enter a committed transit state they cannot be swayed off course. This is meant to reflect a reality where once a task is given it is set out to be complete. In addition, having it go from planned to committed only at tick() prevents old plans from being uneditable- this lets us update them and change our arrivals and cargo before committing.

**Container Choices**  
I track ticks and arrivalTime with an int64 since I expect whole values, but notably I have a hashmap for pending state changes/actions and the list of vessels in the fleet.  I also chose to use a min\_heap for those arriving.

The hashmaps for vessels allows me to easily find and update vessels using their id with average O(1) look-up time. pendingVessels also uses a hashmap in order to store each vessel’s most recent planned state, but it should get cleared every time we advance a tick. These are kind of like “floodgates” that let all the decisions accumulate before updating the vessels cargo or entering the arrivalQueue (if going from idle to in transit) which is important because we only want to be pushing vessels in, or checking the top item in the queue. I originally was going to use an array or vector that held a list of vessels in transit, but I realized checking if they were no longer in transit by iterating through that list was O(n). The min\_heap was better as it keeps the earliest arrivals at the top for me with O(log n) insertion time and similar removal. That way I can pop the vessels that have arrived easily and put them back into being idle. Importantly we can’t change their transit when already moving so this works fine.

I chose to use a vector for docks because there is only one dock for each location, and the locations are guaranteed to be 0,1,2,...n-1. This means we can easily call docks\[location\] to find the one we need preventing a hashmap from being required.   
**Edge Cases**  
I do my best to handle the edge cases within the functions themselves as I am more aware of errors that can arise. If I somehow don’t catch the general issue however, the functions return an error JSON message if it fails to meet expected formatting or specified exceptions to the functions. Because I’m using temporary containers when initializing, and temporary docks for tick manifests and copies of vessels it allows me to prevent the operation from having permanent effects altogether.   
**Docks and Cargos**  
Inside of our dock struct we have throughput which is our capacity for actions at the start of a new phase between ticks, and remainingActions which is the actual actions we have left available to us in that phase. Each time tick() is successfully called it should reset remainingActions back to throughput. 

For our cargo in order to mirror the spec we made it so they could combine when sharing a destination, merging their quantities together. This prevents having a lot of clutter and we remove zero-quantity entries. Because we use a vector to maintain our cargo order, finding the location for it does require a linear search that is O(k) where k is the number of locations our cargo vector holds. 

I chose for the separate load and unload operations to cost double the swap for both simplicity and to mirror real world efficiency. If you send out an order to swap cargo, crew members can work on facilitating the two happening in sync and maximize the efficiency. However if one is called then the next, they do not happen in parallel but instead in sequence.

Cargo operations followed by undock is ok, as cargo can easily be transported onto a ship while it is docked at the location. Importantly though, if a ship is undocked it is no longer connected to land and should not be able to receive or unload their current cargo, even if they are within the location. 

I also chose to have the same-location destination replace pending trips. This offers a way for controllers of the fleet to cancel a trip they wanted, and allows it to be cleanly resolved as it will be immediately popped off the minimum queue and then immediately set back to idle once tick() goes through.

Cargo is designed so unloading at destination removes it, otherwise it gets added to that dock’s inventory. In addition part of my design decision made it so if you are pending either a dock or an undock you cannot load or unload cargo from your ship.

**Testing**  
The unit\_tests run unit tests on each of the functions. They should cover a few successful cases and also cases that trigger every desired error.

test\_\*.hpp and cpp files work together to test the function defined:  
test\_init tests init().  
test\_movement tests advancing through ticks and updating states and manifest cargo, resetting dock throughput, and cases related to transit and arrivals.  
test\_docking has a focus on docking and undocking tests.  
test\_cargo focuses on moving cargo, updating quantities, swapping, and counting the operations along with rejecting unavailable cargo and reaching the limit of throughput.  
test\_get\_state\_at tests the get\_state\_at() function.

The test\_follow\_on1 uses the example given to us in the spec.  
test\_protocol is additional program-level testing.

