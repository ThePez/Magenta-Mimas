using UnityEngine;

public class PlayerRotateWithInertia : MonoBehaviour
{
    void Update()
    {
        float velocity = 4f;          // Current velocity of rotation (measured in deg/s)
        transform.Rotate(Vector3.up, velocity * Time.deltaTime);
    }
}
