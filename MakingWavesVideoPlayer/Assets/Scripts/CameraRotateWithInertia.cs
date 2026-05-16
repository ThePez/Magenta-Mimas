using UnityEngine;

public class CameraRotateWithInertia : MonoBehaviour
{
    [Header("Rotation Settings")]
    private float velocity = 2f;          // Current velocity of rotation

    void Update()
    {
        // Rotate the camera around the Y-axis only (yaw), without affecting the Z-axis (roll)
        Vector3 currentRotation = transform.rotation.eulerAngles;
        float newYRotation = currentRotation.y + velocity * Time.deltaTime;

        // Apply the rotation while keeping the X and Z axes intact
        transform.rotation = Quaternion.Euler(currentRotation.x, newYRotation, currentRotation.z);
    }
}
