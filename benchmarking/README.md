Going to test again resends vs no resends
using a basic placeholder mechanism for missing bits

I will be using pixels for an image to make it visible

right now it just runs 5 iterations of making averages based on adjacent somethings (pixels? idk) ignoring blanks. Each iteration fills
blanks that have remained. kind of works

og:
<img width="626" height="574" alt="Screenshot 2026-05-19 at 9 19 20 PM" src="https://github.com/user-attachments/assets/79a74e8a-7230-4170-9bb9-d32aa963f960" />

no filling:
<img width="1186" height="1046" alt="image" src="https://github.com/user-attachments/assets/ee05d89d-7ccb-4c0b-af90-d6dbab945885" />

filled:
<img width="1186" height="1074" alt="image" src="https://github.com/user-attachments/assets/17c49e3f-d147-4a12-84ef-44513766f48d" />
not the same as the og, notice the red streaks on the right and left of the star. additionally doing a manual pixel check with compare.cpp shows they have a 50% diff.

Later, have plans to test optimizing bits used in data structures and the protocol
to see speed changes under time limits. (perhaps a race, see how much of the image
gets finished for the unreliable protocol vs reliable and how the end result ends up
looking)

