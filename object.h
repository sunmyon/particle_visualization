extern int g_selectedAxis;
extern bool g_flagShowCuboid;
extern TrackingVector<glm::vec3> g_cubicPoints;

void UpdateCuboidTransformArcball(glm::vec3 &center, glm::quat &cuboidTransform
				  , float lastX, float lastY, float xpos, float ypos
				  , const glm::mat4 &view, const glm::vec3 &pivotWorld);
